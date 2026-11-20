#pragma once

#include <cstdint>

#include "lvgl.h"

/**
 * Options for openDialog().
 */
struct DialogOptions {
    const char* title = "";    // Heading
    const char* subtitle = ""; // One line under the heading explaining what the dialog shows; empty for none
    int32_t width = 880;       // Dialog width; the height follows the content up to most of the screen
};

/**
 * Opens a dialog on the overlay layer: a dimmed backdrop that catches every touch, and a raised panel with the title, an
 * optional subtitle, a close (X) button and a body that scrolls when its content is taller than the screen allows. Tapping the backdrop or
 * X closes it; so does any navigation, since the overlay layer is emptied then.
 *
 * Fill the returned body with content. Shared by the fault reactions view and, from FLT-006, the confirm dialog.
 *
 * @param overlayLayer Navigator::getOverlayLayer()
 * @param options Title, subtitle and width
 * @return The body, a column to add content to
 */
lv_obj_t* openDialog(lv_obj_t* overlayLayer, const DialogOptions& options);

/**
 * Adds a row of action buttons under a dialog's body, outside the scrolling part so they stay in view.
 *
 * @param body Body from openDialog()
 * @return The row; add buttons to it
 */
lv_obj_t* addDialogActions(lv_obj_t* body);

/**
 * Closes the dialog an object belongs to, after the current event (safe from the dialog's own button handlers).
 *
 * @param objectInDialog Any object inside the dialog, e.g. its body or a button in it
 */
void closeDialog(lv_obj_t* objectInDialog);
