#include "Screenshot.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "drivers/DisplayLock.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char* TAG = "Screenshot";

/**
 * Blends an ARGB8888 layer over an RGB565 image of the same size.
 *
 * @param base RGB565 image, modified in place
 * @param overlay ARGB8888 layer (LVGL byte order B, G, R, A)
 */
static void blendOverlay(lv_draw_buf_t* base, const lv_draw_buf_t* overlay) {
    uint32_t width = base->header.w;
    uint32_t height = base->header.h;

    for (uint32_t y = 0; y < height; y++) {
        uint16_t* baseRow = reinterpret_cast<uint16_t*>(base->data + y * base->header.stride);
        const uint8_t* overlayRow = overlay->data + y * overlay->header.stride;

        for (uint32_t x = 0; x < width; x++) {
            const uint8_t* overlayPixel = overlayRow + x * 4;
            uint32_t alpha = overlayPixel[3];

            // Most of the top layer is transparent
            if (alpha == 0) {
                continue;
            }

            uint16_t basePixel = baseRow[x];
            uint32_t baseRed = ((basePixel >> 11) & 0x1F) << 3;
            uint32_t baseGreen = ((basePixel >> 5) & 0x3F) << 2;
            uint32_t baseBlue = (basePixel & 0x1F) << 3;

            uint32_t red = (overlayPixel[2] * alpha + baseRed * (255 - alpha)) / 255;
            uint32_t green = (overlayPixel[1] * alpha + baseGreen * (255 - alpha)) / 255;
            uint32_t blue = (overlayPixel[0] * alpha + baseBlue * (255 - alpha)) / 255;

            baseRow[x] = static_cast<uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
        }
    }
}

/**
 * Run-length encodes an RGB565 image into pairs of (run length, pixel), both little-endian uint16.
 *
 * @param image RGB565 image
 * @param output Buffer of at least width × height × 4 bytes (worst case, no runs)
 * @return Number of bytes written
 */
static size_t encodeRunLength(const lv_draw_buf_t* image, uint8_t* output) {
    size_t outputLength = 0;
    uint16_t runPixel = 0;
    uint32_t runLength = 0;

    auto writeRun = [&]() {
        output[outputLength++] = static_cast<uint8_t>(runLength);
        output[outputLength++] = static_cast<uint8_t>(runLength >> 8);
        output[outputLength++] = static_cast<uint8_t>(runPixel);
        output[outputLength++] = static_cast<uint8_t>(runPixel >> 8);
    };

    for (uint32_t y = 0; y < image->header.h; y++) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(image->data + y * image->header.stride);

        for (uint32_t x = 0; x < image->header.w; x++) {
            uint16_t pixel = row[x];

            // Runs continue across rows; a run length fits in 16 bits
            if (runLength > 0 && pixel == runPixel && runLength < 0xFFFF) {
                runLength++;

                continue;
            }

            if (runLength > 0) {
                writeRun();
            }

            runPixel = pixel;
            runLength = 1;
        }
    }

    if (runLength > 0) {
        writeRun();
    }

    return outputLength;
}

bool Screenshot::captureAndSend(uart_port_t uartPort) {
    int64_t startUs = esp_timer_get_time();

    // Render the screen, the top layer and the system layer (the splash) while LVGL is held; the rest works on private
    // buffers
    lv_draw_buf_t* screenImage = nullptr;
    lv_draw_buf_t* topLayerImage = nullptr;
    lv_draw_buf_t* systemLayerImage = nullptr;

    {
        DisplayLock lock;

        if (!lock.isLocked()) {
            return false;
        }

        screenImage = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
        topLayerImage = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);
        systemLayerImage = lv_snapshot_take(lv_layer_sys(), LV_COLOR_FORMAT_ARGB8888);
    }

    if (screenImage == nullptr) {
        ESP_LOGE(TAG, "Snapshot of the screen failed");

        for (lv_draw_buf_t* layerImage : {topLayerImage, systemLayerImage}) {
            if (layerImage != nullptr) {
                lv_draw_buf_destroy(layerImage);
            }
        }

        return false;
    }

    // Top layer first, then the system layer above it
    for (lv_draw_buf_t* layerImage : {topLayerImage, systemLayerImage}) {
        if (layerImage == nullptr) {
            continue;
        }

        if (layerImage->header.w == screenImage->header.w && layerImage->header.h == screenImage->header.h) {
            blendOverlay(screenImage, layerImage);
        }

        lv_draw_buf_destroy(layerImage);
    }

    // Encode into PSRAM; the worst case is twice the image size
    uint32_t width = screenImage->header.w;
    uint32_t height = screenImage->header.h;
    size_t maxPayloadLength = static_cast<size_t>(width) * height * 4;
    uint8_t* payload = static_cast<uint8_t*>(heap_caps_malloc(maxPayloadLength, MALLOC_CAP_SPIRAM));

    if (payload == nullptr) {
        ESP_LOGE(TAG, "Out of memory for %u bytes", static_cast<unsigned>(maxPayloadLength));
        lv_draw_buf_destroy(screenImage);

        return false;
    }

    size_t payloadLength = encodeRunLength(screenImage, payload);
    lv_draw_buf_destroy(screenImage);
    uint32_t crc = esp_rom_crc32_le(0, payload, payloadLength);

    // Send with logging paused, so nothing lands inside the payload
    esp_log_level_set("*", ESP_LOG_NONE);
    uart_wait_tx_done(uartPort, pdMS_TO_TICKS(500));

    char header[96];
    int headerLength = snprintf(header, sizeof(header), "\nSCREENSHOT_BEGIN %" PRIu32 " %" PRIu32 " rgb565-rle %u\n", width, height, static_cast<unsigned>(payloadLength));
    uart_write_bytes(uartPort, header, headerLength);
    uart_write_bytes(uartPort, payload, payloadLength);

    char trailer[48];
    int trailerLength = snprintf(trailer, sizeof(trailer), "\nSCREENSHOT_END %08" PRIx32 "\n", crc);
    uart_write_bytes(uartPort, trailer, trailerLength);
    uart_wait_tx_done(uartPort, pdMS_TO_TICKS(30000));

    esp_log_level_set("*", static_cast<esp_log_level_t>(CONFIG_LOG_DEFAULT_LEVEL));
    heap_caps_free(payload);

    ESP_LOGI(TAG, "Sent %" PRIu32 "x%" PRIu32 " screenshot, %u bytes, in %" PRId64 " ms", width, height, static_cast<unsigned>(payloadLength), (esp_timer_get_time() - startUs) / 1000);

    return true;
}
