#pragma once

#include <cstdint>

/**
 * Holds LVGL's mutex for as long as it lives, so code outside the LVGL task can touch the UI safely.
 *
 * Check isLocked() before using LVGL: taking the lock can time out if the UI is stuck.
 *
 * @code
 * DisplayLock lock;
 *
 * if (lock.isLocked()) {
 *     navigator.open(ScreenId::HOME);
 * }
 * @endcode
 */
class DisplayLock {
  public:
    /**
     * Takes the lock, waiting at most the given time.
     *
     * @param timeoutMs Longest wait; DisplayLock::WAIT_FOREVER waits as long as it takes
     */
    explicit DisplayLock(int32_t timeoutMs = DEFAULT_TIMEOUT_MS);

    /**
     * Releases the lock if it was taken.
     */
    ~DisplayLock();

    DisplayLock(const DisplayLock&) = delete;
    DisplayLock& operator=(const DisplayLock&) = delete;

    /**
     * Returns whether the lock was taken.
     *
     * @return True if LVGL may be used
     */
    bool isLocked() const;

    // Constants
    static constexpr int32_t DEFAULT_TIMEOUT_MS = 2000; // Give up rather than block a caller on a stuck UI
    static constexpr int32_t WAIT_FOREVER = -1;

  private:
    bool hasLock = false;
};
