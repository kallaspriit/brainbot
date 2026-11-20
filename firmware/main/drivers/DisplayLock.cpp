#include "DisplayLock.hpp"

#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char* TAG = "DisplayLock";

DisplayLock::DisplayLock(int32_t timeoutMs)
    : hasLock(bsp_display_lock(timeoutMs)) {
    if (!hasLock) {
        ESP_LOGW(TAG, "Display lock timed out after %d ms", static_cast<int>(timeoutMs));
    }
}

DisplayLock::~DisplayLock() {
    if (hasLock) {
        bsp_display_unlock();
    }
}

bool DisplayLock::isLocked() const {
    return hasLock;
}
