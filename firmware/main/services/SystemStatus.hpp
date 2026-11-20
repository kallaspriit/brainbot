#pragma once

/**
 * Which parts of the console started. Written once by App::begin() before the UI exists, read-only afterwards, so it
 * needs no lock.
 */
struct SystemStatus {
    bool isDisplayStarted = false; // Display, touch and LVGL
    bool isCanStarted = false;     // CAN controller enabled
};
