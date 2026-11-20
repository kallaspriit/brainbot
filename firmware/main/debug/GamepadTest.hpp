#pragma once

#include <cstdint>

// THROWAWAY (test/gamepad branch): Xbox controller over BLE through the C6, shown on a full-screen overlay.
// Open and close run in the LVGL task (or under the display lock). Pair, forget, disconnect and the pairing mode only
// set a request, which the gamepad connection task carries out between scans and connects.

bool gamepadTestStart(); // Bring up the C6 link, NimBLE and the HID host (once); not under the display lock
void gamepadTestOpen();

/**
 * Decoded controller input, as the tank test reads it.
 */
struct GamepadInput {
    bool isConnected = false; // Subscribed to the input report
    uint16_t leftX = 32768;   // 0 = left, 0x8000 = centre
    uint16_t leftY = 32768;   // 0 = up, 0x8000 = centre
    uint16_t rightX = 32768;
    uint16_t rightY = 32768;
    uint16_t leftTrigger = 0; // 0-1023
    uint16_t rightTrigger = 0;
    uint8_t hat = 0;          // 0 centred, 1 up, clockwise to 8 up-left
    uint32_t buttons = 0;     // GAMEPAD_BUTTON_* bits
    uint32_t reportCount = 0; // Grows with every input report
};

// Button bits in GamepadInput::buttons (Xbox BLE report bytes 13-14, checked on a 1708)
constexpr uint32_t GAMEPAD_BUTTON_A = 1u << 0;
constexpr uint32_t GAMEPAD_BUTTON_B = 1u << 1;
constexpr uint32_t GAMEPAD_BUTTON_X = 1u << 3;
constexpr uint32_t GAMEPAD_BUTTON_Y = 1u << 4;
constexpr uint32_t GAMEPAD_BUTTON_LB = 1u << 6;
constexpr uint32_t GAMEPAD_BUTTON_RB = 1u << 7;
constexpr uint32_t GAMEPAD_BUTTON_VIEW = 1u << 10;
constexpr uint32_t GAMEPAD_BUTTON_MENU = 1u << 11;

/**
 * Copies the latest controller input; safe from any task.
 *
 * @param input Filled with the latest input; neutral while disconnected
 */
void gamepadTestGetInput(GamepadInput& input);

/**
 * Sets a second status line on the gamepad view (the tank test's state); safe from any task.
 *
 * @param text Line to show, copied; empty hides it
 */
void gamepadTestSetSecondStatus(const char* text);

/**
 * Makes a tap on the gamepad view call the handler instead of closing the view (the tank test's STOP).
 *
 * @param handler Called in the LVGL task on a tap; nullptr restores closing the view
 */
void gamepadTestSetTapHandler(void (*handler)());
void gamepadTestClose();
void gamepadTestPair();
void gamepadTestForget();
void gamepadTestDisconnect();
void gamepadTestSetSecureConnections(bool isEnabled);
