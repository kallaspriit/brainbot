#include "HardwareLock.hpp"

mutex_t HardwareLock::mutex_;

void HardwareLock::init() {
    mutex_init(&mutex_);
}
