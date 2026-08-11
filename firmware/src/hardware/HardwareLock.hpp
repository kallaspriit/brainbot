#pragma once

#include <pico/mutex.h>

/**
 * Guards state shared between the two cores.
 *
 * Core 1 runs the control tick; core 0 runs the console and telemetry. Both
 * reach the servo bus — the tick to command the beam, console commands to poke
 * individual servos — and a half-transmitted packet from one core interleaved
 * with another's is a corrupt frame on a shared single-wire bus.
 *
 * Scope one of these around every servo bus access and every read or write of
 * shared system state:
 *
 *     {
 *         const HardwareLock lock;
 *         servo.writePos(id, position);
 *     }
 *
 * Hold it only for the hardware transaction (about 1 ms), never across a Serial
 * write. Printing under the lock would let a host that stops draining USB stall
 * the control loop, which is exactly the failure the two-core split exists to
 * prevent. Helpers never take the lock themselves, so callers can hold it across
 * several calls without deadlocking.
 */
class HardwareLock {
  public:
    /** Initializes the underlying mutex. Call once, before core 1 starts. */
    static void init();

    HardwareLock() {
        mutex_enter_blocking(&mutex_);
    }

    ~HardwareLock() {
        mutex_exit(&mutex_);
    }

    HardwareLock(const HardwareLock&) = delete;
    HardwareLock& operator=(const HardwareLock&) = delete;

  private:
    static mutex_t mutex_;
};
