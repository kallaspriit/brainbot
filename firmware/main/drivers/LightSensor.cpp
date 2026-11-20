#include "LightSensor.hpp"

#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "LightSensor";

LightSensor::LightSensor(gpio_num_t pinSensor)
    : pinSensor(pinSensor) {
}

esp_err_t LightSensor::start() {
    if (isStarted) {
        return ESP_OK;
    }

    // The handle once: the pin, the pool and the pattern don't change
    if (handle == nullptr) {
        adc_unit_t unit = ADC_UNIT_1;
        adc_channel_t channel = ADC_CHANNEL_0;
        esp_err_t result = adc_continuous_io_to_channel(pinSensor, &unit, &channel);

        if (result != ESP_OK) {
            ESP_LOGE(TAG, "GPIO%d is not an ADC pin: %s", pinSensor, esp_err_to_name(result));

            return result;
        }

        adc_continuous_handle_cfg_t handleConfig = {};
        handleConfig.max_store_buf_size = FRAME_BYTES * POOL_FRAMES;
        handleConfig.conv_frame_size = FRAME_BYTES;
        result = adc_continuous_new_handle(&handleConfig, &handle);

        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Creating the ADC handle failed: %s", esp_err_to_name(result));
            handle = nullptr;

            return result;
        }

        // One channel, full range: the sensor's output reaches its 3.3 V supply in the dark
        adc_digi_pattern_config_t pattern = {};
        pattern.atten = ADC_ATTEN_DB_12;
        pattern.channel = static_cast<uint8_t>(channel);
        pattern.unit = static_cast<uint8_t>(unit);
        pattern.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

        adc_continuous_config_t config = {};
        config.pattern_num = 1;
        config.adc_pattern = &pattern;
        config.sample_freq_hz = SAMPLE_RATE_HZ;
        config.conv_mode = unit == ADC_UNIT_2 ? ADC_CONV_SINGLE_UNIT_2 : ADC_CONV_SINGLE_UNIT_1;
        result = adc_continuous_config(handle, &config);

        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Configuring the ADC failed: %s", esp_err_to_name(result));

            return result;
        }

        frameTimes = xQueueCreate(FRAME_TIME_QUEUE_LENGTH, sizeof(int64_t));

        adc_continuous_evt_cbs_t callbacks = {};
        callbacks.on_conv_done = onFrameDone;
        callbacks.on_pool_ovf = onPoolOverflow;
        result = adc_continuous_register_event_callbacks(handle, &callbacks, this);

        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Registering the ADC callbacks failed: %s", esp_err_to_name(result));

            return result;
        }

        ESP_LOGI(TAG, "GPIO%d is ADC%d channel %d, %" PRIu32 " Hz, %u samples per frame", pinSensor, unit + 1, channel, SAMPLE_RATE_HZ, static_cast<unsigned>(FRAME_SAMPLES));
    }

    xQueueReset(frameTimes);
    droppedFrameCount = 0;
    esp_err_t result = adc_continuous_start(handle);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Starting the ADC failed: %s", esp_err_to_name(result));

        return result;
    }

    isStarted = true;

    return ESP_OK;
}

void LightSensor::stop() {
    if (!isStarted) {
        return;
    }

    adc_continuous_stop(handle);
    adc_continuous_flush_pool(handle);
    xQueueReset(frameTimes);
    isStarted = false;
}

bool LightSensor::isRunning() const {
    return isStarted;
}

bool LightSensor::readFrame(uint16_t* samples, int64_t& lastSampleUs, uint32_t timeoutMs) {
    static adc_continuous_data_t parsed[FRAME_SAMPLES]; // One reader task; too big for its stack

    if (!isStarted) {
        return false;
    }

    // The frame's time first: the ISR queues it when the frame lands in the pool, so the data is there once it arrives
    if (xQueueReceive(frameTimes, &lastSampleUs, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }

    uint32_t parsedCount = 0;
    esp_err_t result = adc_continuous_read_parse(handle, parsed, FRAME_SAMPLES, &parsedCount, 0);

    if (result != ESP_OK || parsedCount != FRAME_SAMPLES) {
        // The pool and the time queue disagree (an overflow dropped frames): start both afresh
        adc_continuous_flush_pool(handle);
        xQueueReset(frameTimes);
        droppedFrameCount = droppedFrameCount + 1;

        return false;
    }

    for (size_t index = 0; index < FRAME_SAMPLES; index++) {
        samples[index] = static_cast<uint16_t>(parsed[index].valid ? parsed[index].raw_data : 0);
    }

    return true;
}

uint32_t LightSensor::getDroppedFrameCount() const {
    return droppedFrameCount;
}

bool IRAM_ATTR LightSensor::onFrameDone(adc_continuous_handle_t handle, const adc_continuous_evt_data_t* data, void* userData) {
    LightSensor* sensor = static_cast<LightSensor*>(userData);
    int64_t nowUs = esp_timer_get_time();
    BaseType_t isWoken = pdFALSE;

    if (xQueueSendFromISR(sensor->frameTimes, &nowUs, &isWoken) != pdTRUE) {
        sensor->droppedFrameCount = sensor->droppedFrameCount + 1;
    }

    return isWoken == pdTRUE;
}

bool IRAM_ATTR LightSensor::onPoolOverflow(adc_continuous_handle_t handle, const adc_continuous_evt_data_t* data, void* userData) {
    LightSensor* sensor = static_cast<LightSensor*>(userData);
    sensor->droppedFrameCount = sensor->droppedFrameCount + 1;

    return false;
}
