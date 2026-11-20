#pragma once

#include <cstdint>

/**
 * Classic CAN frame with up to 8 data bytes.
 */
struct CanFrame {
    uint32_t id = 0;      // 11-bit identifier
    uint8_t length = 0;   // Number of data bytes (0-8)
    uint8_t data[8] = {}; // Payload
};

/**
 * What protocol code needs from a CAN bus: send, receive, and drop stale frames.
 *
 * Implemented by CanBus on the real controller; protocol code depends only on this, so it can later run against a
 * fake bus in PC tests.
 */
class CanPort {
  public:
    virtual ~CanPort() = default;

    /**
     * Sends a frame and waits until it has been transmitted or has failed.
     *
     * @param frame Frame to send
     * @param timeoutMs Maximum time to wait
     * @return True if the frame was acknowledged on the bus
     */
    virtual bool send(const CanFrame& frame, int timeoutMs) = 0;

    /**
     * Waits for the next received frame.
     *
     * @param frame Receives the frame
     * @param timeoutMs Maximum time to wait
     * @return True if a frame was received
     */
    virtual bool receive(CanFrame& frame, int timeoutMs) = 0;
};
