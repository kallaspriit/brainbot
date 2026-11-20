#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * The latency light sensor (D-034): continuous ADC sampling of one pin into DMA frames, each frame stamped with the
 * `esp_timer` time at which its last sample was converted (Latency measurement plan, D3).
 *
 * Raw counts, 12 bit, 0-4095 over about 0-3.1 V (12 dB attenuation). The DFR0026's output rises with light. Start and
 * stop from one task; read from that task too.
 */
class LightSensor {
  public:
    /**
     * Creates the driver; nothing is configured until start().
     *
     * @param pinSensor ADC-capable pin of the sensor's output (GPIO49 = ADC2 channel 0 on the P4)
     */
    explicit LightSensor(gpio_num_t pinSensor);

    /**
     * Starts sampling. Creates the ADC handle the first time.
     *
     * @return ESP_OK, or the ESP-IDF error of the step that failed (logged)
     */
    esp_err_t start();

    /**
     * Stops sampling and drops what is queued.
     */
    void stop();

    /**
     * Tells whether sampling runs.
     *
     * @return True between a successful start() and stop()
     */
    bool isRunning() const;

    /**
     * Reads the next frame of samples, oldest first.
     *
     * @param samples Receives FRAME_SAMPLES raw counts
     * @param lastSampleUs Receives the `esp_timer` time of the frame's last sample
     * @param timeoutMs How long to wait for a frame
     * @return True if a whole frame was read; false on a timeout or while stopped
     */
    bool readFrame(uint16_t* samples, int64_t& lastSampleUs, uint32_t timeoutMs);

    /**
     * Returns how many frames were dropped because the reader fell behind, since start().
     *
     * @return Dropped frame count
     */
    uint32_t getDroppedFrameCount() const;

    // Constants
    static constexpr uint32_t SAMPLE_RATE_HZ = 20000; // 0.05 ms resolution (plan D3); the P4 allows 611 Hz-83.3 kHz
    static constexpr size_t FRAME_SAMPLES = 256;      // 12.8 ms per frame at 20 kHz

  private:
    gpio_num_t pinSensor;
    adc_continuous_handle_t handle = nullptr;
    QueueHandle_t frameTimes = nullptr; // Frame-done times from the ISR, one per frame, in order
    bool isStarted = false;
    volatile uint32_t droppedFrameCount = 0;

    /**
     * Frame-done callback, in ISR context: queues the time.
     *
     * @param handle The ADC handle
     * @param data The event data
     * @param userData The LightSensor
     * @return True if a higher-priority task was woken
     */
    static bool IRAM_ATTR onFrameDone(adc_continuous_handle_t handle, const adc_continuous_evt_data_t* data, void* userData);

    /**
     * Pool-overflow callback, in ISR context: the reader fell behind.
     *
     * @param handle The ADC handle
     * @param data The event data
     * @param userData The LightSensor
     * @return False; nothing to wake
     */
    static bool IRAM_ATTR onPoolOverflow(adc_continuous_handle_t handle, const adc_continuous_evt_data_t* data, void* userData);

    // Constants
    static constexpr size_t FRAME_BYTES = FRAME_SAMPLES * SOC_ADC_DIGI_RESULT_BYTES;
    static constexpr size_t POOL_FRAMES = 8; // About 100 ms of buffering
    static constexpr UBaseType_t FRAME_TIME_QUEUE_LENGTH = POOL_FRAMES * 2;
};
