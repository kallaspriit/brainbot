// THROWAWAY (test/gamepad branch): Xbox controller over BLE through the C6 (ESP-Hosted HCI), shown on a full-screen overlay

#include "GamepadTest.hpp"

#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_hosted_bt_host_stack.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "lvgl.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

extern "C" void ble_store_config_init(void);

static const char* TAG = "Gamepad";

// Shared state, written by the NimBLE host task, read by the LVGL timer
struct GamepadState {
    char status[64] = "Off";
    bool isConnected = false;
    uint16_t leftX = 32768;
    uint16_t leftY = 32768;
    uint16_t rightX = 32768;
    uint16_t rightY = 32768;
    uint16_t leftTrigger = 0;
    uint16_t rightTrigger = 0;
    uint8_t hat = 0;
    uint32_t buttons = 0;
    uint8_t raw[24] = {};
    size_t rawLength = 0;
    uint16_t reportHandle = 0;
    uint32_t reportCount = 0;
};

static GamepadState state;
static portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;

static bool isStarted = false;
static volatile bool isSynced = false;
static volatile bool isPairRequested = false;
static volatile bool isForgetRequested = false;
static volatile bool isDisconnectRequested = false;
static volatile int secureConnectionsRequest = -1; // -1 none, 0 legacy pairing, 1 LE Secure Connections
static SemaphoreHandle_t scanDone = nullptr;
static ble_addr_t foundAddress = {};
static bool hasFound = false;
static int foundRssi = -127;
static bool isReconnectScan = false;
static ble_addr_t bondedAddress = {};

// Overlay
static lv_obj_t* overlay = nullptr;
static lv_obj_t* statusLabel = nullptr;
static lv_obj_t* secondStatusLabel = nullptr;
static char secondStatus[96] = {};
static void (*tapHandler)() = nullptr;
static lv_obj_t* rawLabel = nullptr;
static lv_obj_t* leftStickDot = nullptr;
static lv_obj_t* rightStickDot = nullptr;
static lv_obj_t* leftTriggerBar = nullptr;
static lv_obj_t* rightTriggerBar = nullptr;
static lv_timer_t* refreshTimer = nullptr;

// Button bits in the Xbox BLE report (firmware 5.x layout as Bluepad32 decodes it; checked on a 1708 on 2026-11-11.
// Share is a Series X|S button, so it never lights on the 1708)
struct ButtonDefinition {
    const char* label;
    uint32_t mask;
    int32_t x;
    int32_t y;
    lv_obj_t* box;
};

static ButtonDefinition buttonDefinitions[] = {
    {"Y", 1u << 4, 790, 150, nullptr},
    {"X", 1u << 3, 730, 210, nullptr},
    {"B", 1u << 1, 850, 210, nullptr},
    {"A", 1u << 0, 790, 270, nullptr},
    {"LB", 1u << 6, 120, 80, nullptr},
    {"RB", 1u << 7, 830, 80, nullptr},
    {"View", 1u << 10, 400, 150, nullptr},
    {"Xbox", 1u << 12, 480, 150, nullptr},
    {"Menu", 1u << 11, 560, 150, nullptr},
    {"Share", 1u << 16, 480, 220, nullptr},
    {"LS", 1u << 13, 120, 500, nullptr},
    {"RS", 1u << 14, 830, 500, nullptr},
};

// D-pad boxes light from the hat value (1 = up, clockwise to 8 = up-left, 0 = centred)
struct HatDefinition {
    const char* label;
    int32_t x;
    int32_t y;
    lv_obj_t* box;
};

static HatDefinition hatDefinitions[] = {
    {"U", 330, 300, nullptr},
    {"R", 390, 360, nullptr},
    {"D", 330, 420, nullptr},
    {"L", 270, 360, nullptr},
};

static void setStatus(const char* status) {
    portENTER_CRITICAL(&stateLock);
    snprintf(state.status, sizeof(state.status), "%s", status);
    portEXIT_CRITICAL(&stateLock);
    ESP_LOGI(TAG, "%s", status);
}

static uint16_t readU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

// Own GATT client (esp_hidh in ESP-IDF 5.5.5 reads no report map or reports from this controller and hangs when the
// link drops mid-discovery): encrypt, find the HID service, subscribe to every notifying characteristic
static constexpr uint16_t UUID_HID_SERVICE = 0x1812;
static constexpr uint16_t UUID_REPORT_MAP = 0x2A4B;
static constexpr uint16_t UUID_CCCD = 0x2902;
static constexpr int MAX_REPORTS = 8;
static constexpr uint16_t SUPERVISION_TIMEOUT_10MS = 50; // 0.5 s; the default was 2.56 s
static const ble_uuid16_t HID_SERVICE_UUID = {{BLE_UUID_TYPE_16}, UUID_HID_SERVICE};

struct NotifyCharacteristic {
    uint16_t valueHandle;
    uint16_t endHandle;
    uint16_t cccdHandle;
};

static uint16_t connectionHandle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool isConnecting = false;
static uint16_t hidStartHandle = 0;
static uint16_t hidEndHandle = 0;
static uint16_t reportMapHandle = 0;
static uint16_t reportMapLength = 0;
static NotifyCharacteristic reports[MAX_REPORTS] = {};
static int reportCount = 0;
static int reportIndex = 0;
static char statusText[64] = {};

static void setConnected(bool isConnected) {
    portENTER_CRITICAL(&stateLock);
    state.isConnected = isConnected;

    // Drop the last input on disconnect, so nothing reads as still held
    if (!isConnected) {
        state.leftX = 32768;
        state.leftY = 32768;
        state.rightX = 32768;
        state.rightY = 32768;
        state.leftTrigger = 0;
        state.rightTrigger = 0;
        state.hat = 0;
        state.buttons = 0;
    }

    portEXIT_CRITICAL(&stateLock);
}

static void decodeReport(uint16_t handle, const uint8_t* data, size_t length) {
    portENTER_CRITICAL(&stateLock);
    state.reportHandle = handle;
    state.rawLength = length < sizeof(state.raw) ? length : sizeof(state.raw);
    memcpy(state.raw, data, state.rawLength);
    state.reportCount++;

    // The main input report: sticks (0x8000 centre), 10-bit triggers, hat, buttons, Share (Series X|S only)
    if (length >= 15) {
        state.leftX = readU16(data + 0);
        state.leftY = readU16(data + 2);
        state.rightX = readU16(data + 4);
        state.rightY = readU16(data + 6);
        state.leftTrigger = readU16(data + 8);
        state.rightTrigger = readU16(data + 10);
        state.hat = data[12];
        state.buttons = readU16(data + 13);

        if (length >= 16) {
            state.buttons |= static_cast<uint32_t>(data[15] & 0x01) << 16;
        }
    }

    uint32_t count = state.reportCount;
    portEXIT_CRITICAL(&stateLock);

    // The first reports in full, so the layout can be checked against the decode
    if (count <= 3) {
        ESP_LOGI(TAG, "Report on handle %u, %u bytes", handle, static_cast<unsigned>(length));
        ESP_LOG_BUFFER_HEX(TAG, data, length);
    }
}

static void subscribeNext();

static int onCccdWritten(uint16_t, const struct ble_gatt_error* error, struct ble_gatt_attr*, void*) {
    ESP_LOGI(TAG, "Subscribed to handle %u: status %u", reports[reportIndex].valueHandle, error->status);
    reportIndex++;
    subscribeNext();

    return 0;
}

static int onDescriptor(uint16_t, const struct ble_gatt_error* error, uint16_t, const struct ble_gatt_dsc* descriptor, void*) {
    if (error->status == 0) {
        if (ble_uuid_u16(&descriptor->uuid.u) == UUID_CCCD) {
            reports[reportIndex].cccdHandle = descriptor->handle;
        }

        return 0;
    }

    // Done with this characteristic: enable notifications, or move on
    if (reports[reportIndex].cccdHandle != 0) {
        static const uint8_t ENABLE_NOTIFY[2] = {0x01, 0x00};
        ble_gattc_write_flat(connectionHandle, reports[reportIndex].cccdHandle, ENABLE_NOTIFY, sizeof(ENABLE_NOTIFY), onCccdWritten, nullptr);
    } else {
        reportIndex++;
        subscribeNext();
    }

    return 0;
}

static int onReportRead(uint16_t, const struct ble_gatt_error* error, struct ble_gatt_attr* attribute, void*) {
    if (error->status == 0 && attribute != nullptr) {
        uint8_t data[64] = {};
        uint16_t length = 0;
        ble_hs_mbuf_to_flat(attribute->om, data, sizeof(data), &length);
        ESP_LOGI(TAG, "Read input report handle %u: %u bytes", attribute->handle, length);
        ESP_LOG_BUFFER_HEX(TAG, data, length);
    } else {
        ESP_LOGW(TAG, "Input report read failed: %u", error->status);
    }

    return 0;
}

static void subscribeNext() {
    if (reportIndex >= reportCount) {
        setConnected(true);
        setStatus("Connected");

        // Read the input report once, to see whether data flows at all
        if (reportCount > 0) {
            ble_gattc_read(connectionHandle, reports[0].valueHandle, onReportRead, nullptr);
        }

        return;
    }

    NotifyCharacteristic& report = reports[reportIndex];

    if (report.endHandle > report.valueHandle) {
        ble_gattc_disc_all_dscs(connectionHandle, report.valueHandle, report.endHandle, onDescriptor, nullptr);
    } else {
        reportIndex++;
        subscribeNext();
    }
}

static int onReportMapRead(uint16_t, const struct ble_gatt_error* error, struct ble_gatt_attr* attribute, void*) {
    if (error->status == 0 && attribute != nullptr) {
        reportMapLength += OS_MBUF_PKTLEN(attribute->om);

        return 0;
    }

    ESP_LOGI(TAG, "Report map read: %u bytes, status %u", reportMapLength, error->status);
    subscribeNext();

    return 0;
}

static int onCharacteristic(uint16_t, const struct ble_gatt_error* error, const struct ble_gatt_chr* characteristic, void*) {
    if (error->status == 0) {
        ESP_LOGI(TAG, "Characteristic 0x%04x value handle %u properties 0x%02x", ble_uuid_u16(&characteristic->uuid.u), characteristic->val_handle, characteristic->properties);

        // Close the previous notifying characteristic's handle range
        if (reportCount > 0 && reports[reportCount - 1].endHandle == 0) {
            reports[reportCount - 1].endHandle = characteristic->def_handle - 1;
        }

        if (ble_uuid_u16(&characteristic->uuid.u) == UUID_REPORT_MAP) {
            reportMapHandle = characteristic->val_handle;
        }

        if ((characteristic->properties & BLE_GATT_CHR_PROP_NOTIFY) != 0 && reportCount < MAX_REPORTS) {
            reports[reportCount] = {characteristic->val_handle, 0, 0};
            reportCount++;
        }

        return 0;
    }

    if (reportCount > 0 && reports[reportCount - 1].endHandle == 0) {
        reports[reportCount - 1].endHandle = hidEndHandle;
    }

    ESP_LOGI(TAG, "%d notifying characteristics in the HID service", reportCount);
    reportIndex = 0;

    // Read the report map first, as real HID hosts do; some devices only report after it
    if (reportMapHandle != 0) {
        reportMapLength = 0;
        ble_gattc_read_long(connectionHandle, reportMapHandle, 0, onReportMapRead, nullptr);
    } else {
        subscribeNext();
    }

    return 0;
}

static int onService(uint16_t, const struct ble_gatt_error* error, const struct ble_gatt_svc* service, void*) {
    if (error->status == 0) {
        hidStartHandle = service->start_handle;
        hidEndHandle = service->end_handle;

        return 0;
    }

    if (hidStartHandle == 0) {
        setStatus("No HID service");
        ble_gap_terminate(connectionHandle, BLE_ERR_REM_USER_CONN_TERM);

        return 0;
    }

    ESP_LOGI(TAG, "HID service handles %u-%u", hidStartHandle, hidEndHandle);
    reportCount = 0;
    ble_gattc_disc_all_chrs(connectionHandle, hidStartHandle, hidEndHandle, onCharacteristic, nullptr);

    return 0;
}

static int onGapEvent(struct ble_gap_event* event, void*) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                connectionHandle = event->connect.conn_handle;
                setStatus("Encrypting");
                ble_gattc_exchange_mtu(connectionHandle, nullptr, nullptr);
                int result = ble_gap_security_initiate(connectionHandle);

                if (result != 0) {
                    ESP_LOGW(TAG, "ble_gap_security_initiate failed: %d", result);
                }
            } else {
                isConnecting = false;
                snprintf(statusText, sizeof(statusText), "Connect failed (%d)", event->connect.status);
                setStatus(statusText);
            }

            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, "Encryption change: status %d", event->enc_change.status);

            if (event->enc_change.status == 0) {
                setStatus("Discovering");
                hidStartHandle = 0;
                hidEndHandle = 0;
                reportMapHandle = 0;
                ble_gattc_disc_svc_by_uuid(connectionHandle, &HID_SERVICE_UUID.u, onService, nullptr);
            } else {
                snprintf(statusText, sizeof(statusText), "Pairing failed (%d)", event->enc_change.status);
                setStatus(statusText);
                ble_gap_terminate(connectionHandle, BLE_ERR_REM_USER_CONN_TERM);
            }

            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // The controller lost our bond (paired elsewhere): forget it and pair again
            struct ble_gap_conn_desc descriptor = {};
            ble_gap_conn_find(event->repeat_pairing.conn_handle, &descriptor);
            ble_store_util_delete_peer(&descriptor.peer_id_addr);

            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        case BLE_GAP_EVENT_NOTIFY_RX: {
            uint8_t data[64] = {};
            uint16_t length = 0;
            ble_hs_mbuf_to_flat(event->notify_rx.om, data, sizeof(data), &length);
            decodeReport(event->notify_rx.attr_handle, data, length);

            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT:
            connectionHandle = BLE_HS_CONN_HANDLE_NONE;
            isConnecting = false;
            setConnected(false);
            snprintf(statusText, sizeof(statusText), "Disconnected (reason 0x%03x)", event->disconnect.reason);
            setStatus(statusText);

            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        case BLE_GAP_EVENT_L2CAP_UPDATE_REQ: {
            // The controller asks for 3 s; keep a dead link short for the tank test, where the spec allows it (the
            // timeout must exceed twice the interval times one plus the latency; intervals are 1.25 ms, timeouts 10 ms)
            // A link-layer request fills self_params; an L2CAP request leaves it null and NimBLE applies peer_params
            // (a local, so editing it is safe; ble_l2cap_sig.c passes it to ble_gap_update_params when we return 0)
            struct ble_gap_upd_params* parameters = event->conn_update_req.self_params;

            if (parameters == nullptr) {
                parameters = const_cast<struct ble_gap_upd_params*>(event->conn_update_req.peer_params);
            }

            if (parameters == nullptr) {
                return 0;
            }

            uint32_t minimumTimeout10ms = (1u + parameters->latency) * parameters->itvl_max * 25u / 100u + 1u;

            if (parameters->supervision_timeout > SUPERVISION_TIMEOUT_10MS && minimumTimeout10ms < SUPERVISION_TIMEOUT_10MS) {
                parameters->supervision_timeout = SUPERVISION_TIMEOUT_10MS;
            }

            return 0;
        }

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU %u", event->mtu.value);

            return 0;

        default:
            return 0;
    }
}

static void connect(const ble_addr_t& address) {
    uint8_t ownAddressType = 0;
    ble_hs_id_infer_auto(0, &ownAddressType);
    isConnecting = true;

    // Waits up to 30 s for the controller to advertise
    struct ble_gap_conn_params parameters = {};
    parameters.scan_itvl = 16;
    parameters.scan_window = 16;
    parameters.itvl_min = 6;  // 7.5 ms
    parameters.itvl_max = 12; // 15 ms
    parameters.latency = 0;
    parameters.supervision_timeout = SUPERVISION_TIMEOUT_10MS; // A dead controller is a disconnect within 0.5 s
    int result = ble_gap_connect(ownAddressType, &address, 30000, &parameters, onGapEvent, nullptr);

    if (result != 0) {
        isConnecting = false;
        ESP_LOGE(TAG, "ble_gap_connect failed: %d", result);
    }
}

// Wait for a scan to finish; cancel it if the completion event never arrives (seen after clearing bonds mid-scan)
static void waitForScan() {
    if (xSemaphoreTake(scanDone, pdMS_TO_TICKS(32000)) != pdTRUE) {
        ESP_LOGW(TAG, "Scan didn't complete, cancelling");
        ble_gap_disc_cancel();
    }
}

// Scan: keep the strongest advert that offers the HID service or is named like an Xbox controller
static int onScanEvent(struct ble_gap_event* event, void*) {
    if (event->type == BLE_GAP_EVENT_DISC && isReconnectScan) {
        // Reconnect: look for the bonded controller's connectable advert
        const ble_addr_t& address = event->disc.addr;
        bool isBonded = memcmp(address.val, bondedAddress.val, 6) == 0;

        if (isBonded && event->disc.event_type <= BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
            ESP_LOGI(TAG, "Bonded controller advertising (event %d, rssi %d)", event->disc.event_type, event->disc.rssi);
            foundAddress = address;
            hasFound = true;
        }

        return 0;
    }

    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_hs_adv_fields fields = {};

        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }

        bool isHid = false;

        for (int i = 0; i < fields.num_uuids16; i++) {
            if (ble_uuid_u16(&fields.uuids16[i].u) == UUID_HID_SERVICE) {
                isHid = true;
            }
        }

        char name[32] = {};

        if (fields.name != nullptr) {
            size_t length = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
            memcpy(name, fields.name, length);
        }

        bool isXbox = strstr(name, "Xbox") != nullptr;

        if ((isHid || isXbox) && event->disc.rssi > foundRssi) {
            ESP_LOGI(TAG,
                     "Found %s (%s) %02x:%02x:%02x:%02x:%02x:%02x type %d rssi %d",
                     name[0] != 0 ? name : "?",
                     isHid ? "HID" : "name",
                     event->disc.addr.val[5],
                     event->disc.addr.val[4],
                     event->disc.addr.val[3],
                     event->disc.addr.val[2],
                     event->disc.addr.val[1],
                     event->disc.addr.val[0],
                     event->disc.addr.type,
                     event->disc.rssi);
            foundAddress = event->disc.addr;
            foundRssi = event->disc.rssi;
            hasFound = true;

            // Connect right away rather than at the end of the scan: pairing mode doesn't last long
            if (isXbox) {
                ble_gap_disc_cancel();
                xSemaphoreGive(scanDone);
            }
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        xSemaphoreGive(scanDone);
    }

    return 0;
}

static void pair() {
    setStatus("Scanning: hold the pair button until the logo flashes fast");
    hasFound = false;
    foundRssi = -127;

    struct ble_gap_disc_params parameters = {};
    parameters.filter_duplicates = 1;
    parameters.passive = 0;

    uint8_t ownAddressType = 0;
    ble_hs_id_infer_auto(0, &ownAddressType);
    xSemaphoreTake(scanDone, 0);
    int result = ble_gap_disc(ownAddressType, 30000, &parameters, onScanEvent, nullptr);

    if (result != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", result);
        setStatus("Scan failed");

        return;
    }

    waitForScan();

    if (!hasFound) {
        setStatus("No controller found");

        return;
    }

    setStatus("Connecting and pairing");
    connect(foundAddress);
}

// Connection task: pair on request, otherwise keep trying the bonded controller (press its Xbox button to wake it)
static void connectionLoop(void*) {
    while (!isSynced) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    setStatus("Ready: send 'gamepad pair'");

    while (true) {
        // Console requests, carried out here so they never run alongside a scan or connect started by this task
        if (secureConnectionsRequest >= 0) {
            ble_hs_cfg.sm_sc = static_cast<uint8_t>(secureConnectionsRequest);
            ESP_LOGI(TAG, "Pairing with %s", secureConnectionsRequest == 1 ? "LE Secure Connections" : "legacy pairing");
            secureConnectionsRequest = -1;
        }

        if (isDisconnectRequested) {
            isDisconnectRequested = false;

            if (connectionHandle != BLE_HS_CONN_HANDLE_NONE) {
                ble_gap_terminate(connectionHandle, BLE_ERR_REM_USER_CONN_TERM);
            }
        }

        if (isForgetRequested) {
            isForgetRequested = false;

            // Stop waiting for the bonded controller before its bond goes
            if (isConnecting && connectionHandle == BLE_HS_CONN_HANDLE_NONE) {
                ble_gap_conn_cancel();
                isConnecting = false;
            }

            ble_store_clear();
            setStatus("Bonds cleared");
        }

        bool isLinkUp = connectionHandle != BLE_HS_CONN_HANDLE_NONE;

        if (isPairRequested) {
            isPairRequested = false;

            if (!isLinkUp) {
                // Stop waiting for the bonded controller first
                if (isConnecting) {
                    ble_gap_conn_cancel();
                    isConnecting = false;
                    vTaskDelay(pdMS_TO_TICKS(100));
                }

                pair();
            }

            continue;
        }

        if (!isLinkUp && !isConnecting) {
            ble_addr_t peers[4] = {};
            int peerCount = 0;
            ble_store_util_bonded_peers(peers, &peerCount, 4);

            if (peerCount > 0) {
                // Scan in 5 s windows for the bonded controller, then connect to the address it advertises with
                setStatus("Waiting for the bonded controller (press its Xbox button)");
                bondedAddress = peers[0];
                hasFound = false;
                isReconnectScan = true;

                struct ble_gap_disc_params parameters = {};
                parameters.filter_duplicates = 1;
                parameters.passive = 1;
                // Also report directed adverts aimed at a resolvable private address (added while diagnosing reconnects;
                // the fix turned out to be not sending our IRK, see sm_our_key_dist)
                parameters.filter_policy = BLE_HCI_SCAN_FILT_NO_WL_INITA;
                uint8_t ownAddressType = 0;
                ble_hs_id_infer_auto(0, &ownAddressType);

                xSemaphoreTake(scanDone, 0);

                if (ble_gap_disc(ownAddressType, 5000, &parameters, onScanEvent, nullptr) == 0) {
                    waitForScan();
                }

                isReconnectScan = false;

                if (hasFound && !isPairRequested) {
                    setStatus("Reconnecting");
                    connect(foundAddress);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void onSync() {
    uint8_t address[6] = {};
    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, address, nullptr);
    ESP_LOGI(TAG, "NimBLE synced, own address %02x:%02x:%02x:%02x:%02x:%02x", address[5], address[4], address[3], address[2], address[1], address[0]);
    isSynced = true;
}

static void onReset(int reason) {
    ESP_LOGW(TAG, "NimBLE reset, reason %d", reason);
    isSynced = false;
}

static void hostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void logHeap(const char* stage) {
    ESP_LOGI(TAG,
             "%s: internal %u free (largest %u), DMA %u free (largest %u)",
             stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
}

static bool startBluetooth() {
    if (isStarted) {
        return true;
    }

    logHeap("Before Bluetooth");

    // Bonds are kept in NVS
    esp_err_t result = nvs_flash_init();

    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        result = nvs_flash_init();
    }

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(result));

        return false;
    }

    // C6 controller and the HCI pipe to NimBLE. The factory C6 firmware (ESP-Hosted, reports 0.0.0) doesn't answer
    // 3.x RPCs (version, controller init), but its controller is up from boot, so only the HCI pipe is bound
    esp_hosted_bt_host_stack_cfg_t hostedConfig = ESP_HOSTED_BT_HOST_STACK_CONFIG_DEFAULT();
    hostedConfig.bring_up_controller = false;
    result = esp_hosted_bt_host_stack_setup(&hostedConfig);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_bt_host_stack_setup failed: %s", esp_err_to_name(result));

        return false;
    }

    logHeap("After HCI bind");
    result = nimble_port_init();

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(result));

        return false;
    }

    // Just Works pairing with bonding; legacy pairing, as LE Secure Connections fails here (DHKey check failed)
    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.reset_cb = onReset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    // No identity key from us: the controller then aims its reconnect adverts at our public address
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

    scanDone = xSemaphoreCreateBinary();
    nimble_port_freertos_init(hostTask);
    // Stack in PSRAM: internal RAM is nearly full
    xTaskCreateWithCaps(connectionLoop, "gamepad", 6144, nullptr, 3, nullptr, MALLOC_CAP_SPIRAM);
    isStarted = true;
    logHeap("After Bluetooth start");

    return true;
}

// Overlay drawing (LVGL task only)
static lv_obj_t* createBox(lv_obj_t* parent, const char* label, int32_t x, int32_t y, int32_t width) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, width, 48);
    lv_obj_set_pos(box, x - width / 2, y - 24);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x606060), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x202020), 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* text = lv_label_create(box);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_color(text, lv_color_white(), 0);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_16, 0);
    lv_obj_center(text);

    return box;
}

static void setLit(lv_obj_t* box, bool isLit) {
    lv_obj_set_style_bg_color(box, isLit ? lv_color_hex(0x2E9E44) : lv_color_hex(0x202020), 0);
}

static lv_obj_t* createStick(lv_obj_t* parent, int32_t centreX, int32_t centreY) {
    lv_obj_t* ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 180, 180);
    lv_obj_set_pos(ring, centreX - 90, centreY - 90);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x606060), 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* dot = lv_obj_create(ring);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 24, 24);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x3A8DDE), 0);
    lv_obj_center(dot);

    return dot;
}

static lv_obj_t* createTrigger(lv_obj_t* parent, const char* label, int32_t x) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 40, 160);
    lv_obj_set_pos(bar, x - 20, 140);
    lv_bar_set_range(bar, 0, 1023);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* text = lv_label_create(parent);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_color(text, lv_color_white(), 0);
    lv_obj_set_pos(text, x - 10, 310);

    return bar;
}

static void moveDot(lv_obj_t* dot, uint16_t x, uint16_t y) {
    // 0..65535 to -78..78 px around the ring's centre
    int32_t offsetX = (static_cast<int32_t>(x) - 32768) * 78 / 32768;
    int32_t offsetY = (static_cast<int32_t>(y) - 32768) * 78 / 32768;
    lv_obj_align(dot, LV_ALIGN_CENTER, offsetX, offsetY);
}

static void refresh(lv_timer_t*) {
    GamepadState snapshot;
    portENTER_CRITICAL(&stateLock);
    snapshot = state;
    portEXIT_CRITICAL(&stateLock);

    char text[160];
    snprintf(text, sizeof(text), "%s  reports %lu", snapshot.status, static_cast<unsigned long>(snapshot.reportCount));
    lv_label_set_text(statusLabel, text);

    char second[96] = {};
    portENTER_CRITICAL(&stateLock);
    memcpy(second, secondStatus, sizeof(second));
    portEXIT_CRITICAL(&stateLock);
    lv_label_set_text(secondStatusLabel, second);

    char raw[100] = {};
    int offset = snprintf(raw, sizeof(raw), "handle %u:", snapshot.reportHandle);

    for (size_t i = 0; i < snapshot.rawLength && offset < static_cast<int>(sizeof(raw)) - 4; i++) {
        offset += snprintf(raw + offset, sizeof(raw) - offset, " %02x", snapshot.raw[i]);
    }

    lv_label_set_text(rawLabel, raw);

    moveDot(leftStickDot, snapshot.leftX, snapshot.leftY);
    moveDot(rightStickDot, snapshot.rightX, snapshot.rightY);
    lv_bar_set_value(leftTriggerBar, snapshot.leftTrigger, LV_ANIM_OFF);
    lv_bar_set_value(rightTriggerBar, snapshot.rightTrigger, LV_ANIM_OFF);

    for (ButtonDefinition& button : buttonDefinitions) {
        setLit(button.box, (snapshot.buttons & button.mask) != 0);
    }

    // Hat 1..8 clockwise from up
    bool isUp = snapshot.hat == 1 || snapshot.hat == 2 || snapshot.hat == 8;
    bool isRight = snapshot.hat >= 2 && snapshot.hat <= 4;
    bool isDown = snapshot.hat >= 4 && snapshot.hat <= 6;
    bool isLeft = snapshot.hat >= 6 && snapshot.hat <= 8;
    setLit(hatDefinitions[0].box, isUp);
    setLit(hatDefinitions[1].box, isRight);
    setLit(hatDefinitions[2].box, isDown);
    setLit(hatDefinitions[3].box, isLeft);
}

static void openOverlay() {
    if (overlay != nullptr) {
        return;
    }

    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        overlay,
        [](lv_event_t*) {
            if (tapHandler != nullptr) {
                tapHandler();

                return;
            }

            gamepadTestClose();
        },
        LV_EVENT_CLICKED,
        nullptr);

    statusLabel = lv_label_create(overlay);
    lv_obj_set_style_text_color(statusLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(statusLabel, 16, 12);

    secondStatusLabel = lv_label_create(overlay);
    lv_obj_set_style_text_color(secondStatusLabel, lv_color_hex(0xF0B030), 0);
    lv_obj_set_style_text_font(secondStatusLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(secondStatusLabel, 16, 36);

    rawLabel = lv_label_create(overlay);
    lv_obj_set_style_text_color(rawLabel, lv_color_hex(0xA0A0A0), 0);
    lv_obj_set_style_text_font(rawLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(rawLabel, 16, 570);

    leftStickDot = createStick(overlay, 150, 380);
    rightStickDot = createStick(overlay, 640, 430);
    leftTriggerBar = createTrigger(overlay, "LT", 40);
    rightTriggerBar = createTrigger(overlay, "RT", 980);

    for (ButtonDefinition& button : buttonDefinitions) {
        button.box = createBox(overlay, button.label, button.x, button.y, 70);
    }

    for (HatDefinition& hat : hatDefinitions) {
        hat.box = createBox(overlay, hat.label, hat.x, hat.y, 52);
    }

    refreshTimer = lv_timer_create(refresh, 30, nullptr);
    refresh(nullptr);
}

void gamepadTestGetInput(GamepadInput& input) {
    portENTER_CRITICAL(&stateLock);
    input.isConnected = state.isConnected;
    input.leftX = state.leftX;
    input.leftY = state.leftY;
    input.rightX = state.rightX;
    input.rightY = state.rightY;
    input.leftTrigger = state.leftTrigger;
    input.rightTrigger = state.rightTrigger;
    input.hat = state.hat;
    input.buttons = state.buttons;
    input.reportCount = state.reportCount;
    portEXIT_CRITICAL(&stateLock);
}

void gamepadTestSetSecondStatus(const char* text) {
    portENTER_CRITICAL(&stateLock);
    snprintf(secondStatus, sizeof(secondStatus), "%s", text);
    portEXIT_CRITICAL(&stateLock);
}

void gamepadTestSetTapHandler(void (*handler)()) {
    tapHandler = handler;
}

bool gamepadTestStart() {
    return startBluetooth();
}

void gamepadTestOpen() {
    openOverlay();
}

void gamepadTestPair() {
    if (!startBluetooth()) {
        return;
    }

    isPairRequested = true;
}

void gamepadTestDisconnect() {
    isDisconnectRequested = true;
}

void gamepadTestSetSecureConnections(bool isEnabled) {
    secureConnectionsRequest = isEnabled ? 1 : 0;
}

void gamepadTestForget() {
    if (!isStarted) {
        return;
    }

    isForgetRequested = true;
}

void gamepadTestClose() {
    if (refreshTimer != nullptr) {
        lv_timer_delete(refreshTimer);
        refreshTimer = nullptr;
    }

    if (overlay != nullptr) {
        lv_obj_delete_async(overlay);
        overlay = nullptr;
    }
}
