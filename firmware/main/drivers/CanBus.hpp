#pragma once

#include <atomic>
#include <cstdint>

#include "driver/gpio.h"
#include "drivers/CanPort.hpp"
#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

/**
 * Thin wrapper around the ESP-IDF on-chip TWAI driver for classic 11-bit CAN.
 *
 * Received frames are copied from the interrupt into a queue and read with receive(). Sending waits until the frame
 * is on the bus or has failed, so a missing node shows up as a send failure instead of a stuck transmit queue. One
 * frame is in flight at a time; send() and receive() are meant for a single task.
 */
class CanBus : public CanPort {
  public:
    /**
     * Creates the bus wrapper. Call begin() before use.
     *
     * @param pinTx GPIO connected to the transceiver TX input
     * @param pinRx GPIO connected to the transceiver RX output
     * @param bitrate Bus bitrate in bits per second
     */
    CanBus(gpio_num_t pinTx, gpio_num_t pinRx, uint32_t bitrate);

    /**
     * Creates and enables the TWAI node.
     *
     * @return ESP_OK on success, or the driver error
     */
    esp_err_t begin();

    /**
     * Sends a frame and waits until it has been transmitted or has failed.
     *
     * @param frame Frame to send
     * @param timeoutMs Maximum time to wait
     * @return True if the frame was acknowledged on the bus
     */
    bool send(const CanFrame& frame, int timeoutMs) override;

    /**
     * Waits for the next received frame.
     *
     * @param frame Receives the frame
     * @param timeoutMs Maximum time to wait
     * @return True if a frame was received
     */
    bool receive(CanFrame& frame, int timeoutMs) override;

    /**
     * Reads the controller's error state and error counters.
     *
     * @param status Receives the error state and error counters
     */
    void getStatus(twai_node_status_t& status);

    /**
     * Starts bus-off recovery if the controller is bus-off; a bus-off node never transmits again until recovered.
     * Recovery finishes in the background once the bus has been idle long enough.
     *
     * @return True if recovery was started
     */
    bool recoverIfBusOff();

    /**
     * Returns how many received frames were dropped because the receive queue was full.
     *
     * @return Dropped frame count since start
     */
    uint32_t getReceiveDropCount() const;

    /**
     * Restarts the controller at another bitrate, for finding a drive set to one (D-026). Frames waiting to be read
     * are dropped. Call from the task that uses the bus.
     *
     * @param newBitrate Bitrate in bits per second
     * @return True if the controller runs at the new bitrate
     */
    bool setBitrate(uint32_t newBitrate);

    /**
     * Returns the bitrate the controller runs at.
     *
     * @return Bits per second
     */
    uint32_t getBitrate() const;

  private:
    // Hardware
    gpio_num_t pinTx;
    gpio_num_t pinRx;
    uint32_t bitrate;
    twai_node_handle_t node = nullptr;
    QueueHandle_t receiveQueue = nullptr;

    // Frame in flight; the driver reads it until the transmit-done callback, so it can't live on the stack
    twai_frame_t transmitFrame = {};
    uint8_t transmitData[8] = {};
    SemaphoreHandle_t transmitDone = nullptr;       // Given by the transmit-done callback
    std::atomic<bool> isTransmitSuccessful {false}; // Result of the last finished transmission
    bool isTransmitPending = false;                 // A frame is queued and its callback hasn't come yet
    std::atomic<uint32_t> receiveDropCount {0};     // Frames lost to a full receive queue

    /**
     * Creates, configures and enables the TWAI node at the current bitrate.
     *
     * @return ESP_OK on success, or the driver error
     */
    esp_err_t createNode();

    /**
     * Copies a received frame into the receive queue. Runs in interrupt context.
     *
     * @param handle TWAI node handle
     * @param eventData Event data from the driver
     * @param context The CanBus instance
     * @return True if a higher priority task was woken
     */
    static bool onReceiveDone(twai_node_handle_t handle, const twai_rx_done_event_data_t* eventData, void* context);

    /**
     * Records whether the transmission succeeded and wakes send(). Runs in interrupt context.
     *
     * @param handle TWAI node handle
     * @param eventData Event data from the driver
     * @param context The CanBus instance
     * @return True if a higher priority task was woken
     */
    static bool onTransmitDone(twai_node_handle_t handle, const twai_tx_done_event_data_t* eventData, void* context);

    // Constants
    static constexpr int RECEIVE_QUEUE_LENGTH = 32;       // Frames buffered between the interrupt and the reader
    static constexpr int TRANSMIT_RETRY_COUNT = 3;        // Hardware retries before a frame counts as failed (no ACK)
    static constexpr int TRANSMIT_DRAIN_TIMEOUT_MS = 200; // Longest wait for a frame in flight before changing bitrate
    static constexpr int NODE_DELETE_SETTLE_MS = 10;      // Lets the timer task finish the driver's pending updates
};
