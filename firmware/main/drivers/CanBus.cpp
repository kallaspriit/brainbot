#include "CanBus.hpp"

#include <algorithm>

#include "esp_log.h"
#include "esp_twai_onchip.h"

static const char* TAG = "CanBus";

CanBus::CanBus(gpio_num_t pinTx, gpio_num_t pinRx, uint32_t bitrate)
    : pinTx(pinTx)
    , pinRx(pinRx)
    , bitrate(bitrate) {
}

esp_err_t CanBus::begin() {
    receiveQueue = xQueueCreate(RECEIVE_QUEUE_LENGTH, sizeof(CanFrame));
    transmitDone = xSemaphoreCreateBinary();

    if (receiveQueue == nullptr || transmitDone == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = createNode();

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "CAN bus started at %" PRIu32 " bit/s (TX GPIO%d, RX GPIO%d)", bitrate, pinTx, pinRx);
    }

    return result;
}

bool CanBus::setBitrate(uint32_t newBitrate) {
    if (newBitrate == bitrate && node != nullptr) {
        return true;
    }

    // The timing is fixed when the node is created, so replace the node. A frame still being retried (nobody
    // acknowledges it on an empty bus, and at 10 kbit/s one takes over 10 ms) must finish first: deleting the node under
    // it crashed in the driver (spinlock assert, bench 2026-10-11)
    if (node != nullptr) {
        twai_node_transmit_wait_all_done(node, TRANSMIT_DRAIN_TIMEOUT_MS);

        twai_node_status_t status = {};
        twai_node_get_info(node, &status, nullptr);

        // Disabling a node that is already off (bus-off) is an error in the driver
        if (status.state != TWAI_ERROR_BUS_OFF) {
            twai_node_disable(node);
        }

        // The driver's interrupt may have queued an event-group update to the FreeRTOS timer task just before; deleting
        // the node at once frees the group first and the timer task asserts on it (decoded panic, bench 2026-10-11)
        vTaskDelay(pdMS_TO_TICKS(NODE_DELETE_SETTLE_MS));
        twai_node_delete(node);
        node = nullptr;
    }

    bitrate = newBitrate;
    isTransmitPending = false;
    xQueueReset(receiveQueue);
    xSemaphoreTake(transmitDone, 0);

    if (createNode() != ESP_OK) {
        return false;
    }

    ESP_LOGI(TAG, "CAN bus now at %" PRIu32 " bit/s", bitrate);

    return true;
}

uint32_t CanBus::getBitrate() const {
    return bitrate;
}

esp_err_t CanBus::createNode() {
    twai_onchip_node_config_t nodeConfig = {};
    nodeConfig.io_cfg.tx = pinTx;
    nodeConfig.io_cfg.rx = pinRx;
    nodeConfig.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    nodeConfig.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    nodeConfig.bit_timing.bitrate = bitrate;
    nodeConfig.fail_retry_cnt = TRANSMIT_RETRY_COUNT;
    nodeConfig.tx_queue_depth = 4;

    esp_err_t result = twai_new_node_onchip(&nodeConfig, &node);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Creating the TWAI node at %" PRIu32 " bit/s failed: %s", bitrate, esp_err_to_name(result));
        node = nullptr;

        return result;
    }

    // Register callbacks before enabling the node
    twai_event_callbacks_t callbacks = {};
    callbacks.on_rx_done = onReceiveDone;
    callbacks.on_tx_done = onTransmitDone;

    result = twai_node_register_event_callbacks(node, &callbacks, this);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Registering TWAI callbacks failed: %s", esp_err_to_name(result));
        twai_node_delete(node);
        node = nullptr;

        return result;
    }

    result = twai_node_enable(node);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Enabling the TWAI node failed: %s", esp_err_to_name(result));
        twai_node_delete(node);
        node = nullptr;
    }

    return result;
}

bool CanBus::send(const CanFrame& frame, int timeoutMs) {
    if (node == nullptr) {
        return false;
    }

    // A frame that timed out earlier is still owned by the driver (it is retried after bus-off recovery); wait for it
    if (isTransmitPending) {
        if (xSemaphoreTake(transmitDone, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
            return false;
        }

        isTransmitPending = false;
    }

    // Copy into the member frame, which stays valid until the transmit-done callback
    std::copy(frame.data, frame.data + 8, transmitData);
    transmitFrame = {};
    transmitFrame.header.id = frame.id;
    transmitFrame.header.dlc = frame.length;
    transmitFrame.buffer = transmitData;
    transmitFrame.buffer_len = frame.length;

    isTransmitSuccessful = false;
    xSemaphoreTake(transmitDone, 0);

    if (twai_node_transmit(node, &transmitFrame, timeoutMs) != ESP_OK) {
        return false;
    }

    isTransmitPending = true;

    if (xSemaphoreTake(transmitDone, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        return false;
    }

    isTransmitPending = false;

    return isTransmitSuccessful;
}

bool CanBus::receive(CanFrame& frame, int timeoutMs) {
    return xQueueReceive(receiveQueue, &frame, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void CanBus::getStatus(twai_node_status_t& status) {
    status = {};
    status.state = TWAI_ERROR_BUS_OFF;

    if (node != nullptr) {
        twai_node_get_info(node, &status, nullptr);
    }
}

bool CanBus::recoverIfBusOff() {
    twai_node_status_t status;
    getStatus(status);

    if (node == nullptr || status.state != TWAI_ERROR_BUS_OFF) {
        return false;
    }

    ESP_LOGW(TAG, "Bus-off, recovering");

    return twai_node_recover(node) == ESP_OK;
}

uint32_t CanBus::getReceiveDropCount() const {
    return receiveDropCount;
}

bool CanBus::onReceiveDone(twai_node_handle_t handle, const twai_rx_done_event_data_t* eventData, void* context) {
    CanBus* bus = static_cast<CanBus*>(context);

    uint8_t data[8] = {};
    twai_frame_t twaiFrame = {};
    twaiFrame.buffer = data;
    twaiFrame.buffer_len = sizeof(data);

    if (twai_node_receive_from_isr(handle, &twaiFrame) != ESP_OK) {
        return false;
    }

    CanFrame frame;
    frame.id = twaiFrame.header.id;
    frame.length = static_cast<uint8_t>(std::min<uint16_t>(twaiFrame.header.dlc, 8));
    std::copy(data, data + 8, frame.data);

    BaseType_t isHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(bus->receiveQueue, &frame, &isHigherPriorityTaskWoken) != pdTRUE) {
        bus->receiveDropCount++;
    }

    return isHigherPriorityTaskWoken == pdTRUE;
}

bool CanBus::onTransmitDone(twai_node_handle_t handle, const twai_tx_done_event_data_t* eventData, void* context) {
    CanBus* bus = static_cast<CanBus*>(context);
    bus->isTransmitSuccessful = eventData->is_tx_success;

    BaseType_t isHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(bus->transmitDone, &isHigherPriorityTaskWoken);

    return isHigherPriorityTaskWoken == pdTRUE;
}
