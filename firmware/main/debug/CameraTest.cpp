// THROWAWAY (test/camera branch): stream the OV5647 on the MIPI-CSI connector to a full-screen overlay

#include "CameraTest.hpp"

#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "drivers/DisplayLock.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "lvgl.h"

static const char* TAG = "Camera";

static uint32_t frameWidth = 0;
static uint32_t frameHeight = 0;
static constexpr uint32_t BUFFER_COUNT = 2;

static bool isVideoInitialised = false;
static volatile bool isRunning = false;
static TaskHandle_t captureTask = nullptr;
static uint8_t* frameBuffer = nullptr;
static lv_obj_t* overlay = nullptr;
static lv_obj_t* image = nullptr;
static lv_image_dsc_t imageDescriptor = {};

// Initialise esp_video once: the sensor shares the BSP's I2C bus, the display already powers the MIPI PHY LDO
static bool initVideo() {
    if (isVideoInitialised) {
        return true;
    }

    esp_video_init_csi_config_t csiConfig = {};
    csiConfig.sccb_config.init_sccb = false;
    csiConfig.sccb_config.i2c_handle = bsp_i2c_get_handle();
    csiConfig.sccb_config.freq = 100000;
    csiConfig.reset_pin = GPIO_NUM_NC;
    csiConfig.pwdn_pin = GPIO_NUM_NC;
    csiConfig.dont_init_ldo = true;

    esp_video_init_config_t config = {};
    config.csi = &csiConfig;

    esp_err_t result = esp_video_init(&config);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(result));

        return false;
    }

    isVideoInitialised = true;

    return true;
}

static void closeOverlay() {
    if (overlay != nullptr) {
        lv_obj_delete(overlay);
        overlay = nullptr;
        image = nullptr;
    }
}

static void captureLoop(void*) {
    int fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    uint8_t* buffers[BUFFER_COUNT] = {};
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    uint32_t frameCount = 0;
    int64_t windowStartUs = esp_timer_get_time();

    if (fd < 0) {
        ESP_LOGE(TAG, "Can't open %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        goto done;
    }

    {
        struct v4l2_capability capability = {};
        ioctl(fd, VIDIOC_QUERYCAP, &capability);
        ESP_LOGI(TAG, "Driver %s, card %s", capability.driver, capability.card);

        // Keep the sensor's current size, ask the ISP for RGB565
        struct v4l2_format format = {};
        format.type = type;

        if (ioctl(fd, VIDIOC_G_FMT, &format) != 0) {
            ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
            goto done;
        }

        ESP_LOGI(TAG, "Sensor format %lux%lu, fourcc 0x%08lx", static_cast<unsigned long>(format.fmt.pix.width), static_cast<unsigned long>(format.fmt.pix.height), static_cast<unsigned long>(format.fmt.pix.pixelformat));
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;

        if (ioctl(fd, VIDIOC_S_FMT, &format) != 0) {
            ESP_LOGE(TAG, "VIDIOC_S_FMT failed");
            goto done;
        }

        frameWidth = format.fmt.pix.width;
        frameHeight = format.fmt.pix.height;

        // Frame buffer and image, now that the size is known
        {
            DisplayLock lock(DisplayLock::WAIT_FOREVER);

            heap_caps_free(frameBuffer);
            frameBuffer = static_cast<uint8_t*>(heap_caps_aligned_calloc(64, 1, frameWidth * frameHeight * 2, MALLOC_CAP_SPIRAM));
            imageDescriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
            imageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
            imageDescriptor.header.w = frameWidth;
            imageDescriptor.header.h = frameHeight;
            imageDescriptor.header.stride = frameWidth * 2;
            imageDescriptor.data_size = frameWidth * frameHeight * 2;
            imageDescriptor.data = frameBuffer;

            if (overlay != nullptr) {
                image = lv_image_create(overlay);
                lv_image_set_src(image, &imageDescriptor);
                lv_obj_center(image);
            }
        }

        struct v4l2_requestbuffers request = {};
        request.count = BUFFER_COUNT;
        request.type = type;
        request.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_REQBUFS, &request) != 0) {
            ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
            goto done;
        }

        for (uint32_t i = 0; i < BUFFER_COUNT; i++) {
            struct v4l2_buffer buffer = {};
            buffer.type = type;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = i;

            if (ioctl(fd, VIDIOC_QUERYBUF, &buffer) != 0) {
                ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed");
                goto done;
            }

            buffers[i] = static_cast<uint8_t*>(mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer.m.offset));

            if (ioctl(fd, VIDIOC_QBUF, &buffer) != 0) {
                ESP_LOGE(TAG, "VIDIOC_QBUF failed");
                goto done;
            }
        }

        if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
            ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
            goto done;
        }

        ESP_LOGI(TAG, "Streaming %lux%lu RGB565", static_cast<unsigned long>(frameWidth), static_cast<unsigned long>(frameHeight));

        while (isRunning) {
            struct v4l2_buffer buffer = {};
            buffer.type = type;
            buffer.memory = V4L2_MEMORY_MMAP;

            if (ioctl(fd, VIDIOC_DQBUF, &buffer) != 0) {
                ESP_LOGE(TAG, "VIDIOC_DQBUF failed");
                break;
            }

            // Copy the frame and redraw under the display lock
            if ((buffer.flags & V4L2_BUF_FLAG_DONE) != 0) {
                DisplayLock lock(100);

                if (lock.isLocked() && image != nullptr) {
                    memcpy(frameBuffer, buffers[buffer.index], frameWidth * frameHeight * 2);
                    lv_obj_invalidate(image);
                }

                frameCount++;
            }

            ioctl(fd, VIDIOC_QBUF, &buffer);

            // Let other display lock users (screenshots, the console) in between frames
            vTaskDelay(pdMS_TO_TICKS(10));

            // Frame rate once every 2 s
            int64_t nowUs = esp_timer_get_time();

            if (nowUs - windowStartUs >= 2000000) {
                ESP_LOGI(TAG, "%.1f fps", frameCount * 1e6 / (nowUs - windowStartUs));
                frameCount = 0;
                windowStartUs = nowUs;
            }
        }

        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }

done:
    if (fd >= 0) {
        close(fd);
    }

    {
        DisplayLock lock(DisplayLock::WAIT_FOREVER);
        closeOverlay();
    }

    isRunning = false;
    captureTask = nullptr;
    ESP_LOGI(TAG, "Stopped");
    vTaskDelete(nullptr);
}

void cameraTestStart() {
    if (isRunning) {
        ESP_LOGW(TAG, "Already running");

        return;
    }

    if (!initVideo()) {
        return;
    }

    // Full-screen black overlay with the frame centred; tap to stop
    {
        DisplayLock lock;

        if (!lock.isLocked()) {
            return;
        }

        overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(
            overlay,
            [](lv_event_t*) {
                isRunning = false;
            },
            LV_EVENT_CLICKED,
            nullptr);
    }

    isRunning = true;
    xTaskCreatePinnedToCore(captureLoop, "camera", 6144, nullptr, 1, &captureTask, 1);
}

void cameraTestStop() {
    isRunning = false;
}
