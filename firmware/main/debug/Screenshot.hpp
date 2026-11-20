#pragma once

#include "driver/uart.h"

/**
 * Captures the display and sends it over the serial console, for checking UI changes from the PC.
 *
 * The active screen is rendered with lv_snapshot, LVGL's top layer (where the top bar lives) is rendered too and
 * blended over it, and the result is sent as run-length encoded RGB565 in a frame that tools/screenshot.py decodes:
 *
 *     SCREENSHOT_BEGIN <width> <height> rgb565-rle <payload bytes>\n
 *     <payload: pairs of little-endian uint16 run length and uint16 RGB565 pixel>
 *     \nSCREENSHOT_END <crc32 of payload, hex>\n
 *
 * Logging is paused during the transfer so log lines can't land inside the payload.
 */
class Screenshot {
  public:
    /**
     * Captures the display and sends it. Takes the display lock while rendering.
     *
     * @param uartPort UART to send on (the console UART, with its driver installed)
     * @return True if the frame was sent
     */
    static bool captureAndSend(uart_port_t uartPort);
};
