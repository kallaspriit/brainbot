#pragma once

#include <cstddef>

#include "lvgl.h"

using ConfirmCallback = void (*)(void* userData);

/**
 * Options for openConfirmDialog().
 */
struct ConfirmDialogOptions {
    const char* title = "";              // What will happen, e.g. "Restore factory defaults?"
    const char* subtitle = "";           // One line under the title
    const char* const* points = nullptr; // What the operator must know, one line each; must outlive the call
    size_t pointCount = 0;               // Number of points
    const char* writes = "";             // What is written to the drive, e.g. "1011:01 = 0x64616F6C (load)"
    const char* confirmText = "CONFIRM"; // Confirm button text
    ConfirmCallback onConfirm = nullptr; // Called on confirm, after the dialog starts closing
    void* userData = nullptr;            // Passed to onConfirm; must outlive the dialog
};

/**
 * Opens a confirmation for a write to the drive (safety rules: writes are explicit): the dialog frame with the points,
 * what will be written, and CANCEL and a red confirm button. Cancel, the X or a tap outside writes nothing.
 *
 * @param overlayLayer Navigator::getOverlayLayer()
 * @param options Text, the write and the confirm action
 */
void openConfirmDialog(lv_obj_t* overlayLayer, const ConfirmDialogOptions& options);
