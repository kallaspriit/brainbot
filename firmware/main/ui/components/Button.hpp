#pragma once

#include <cstdint>

#include "lvgl.h"

/**
 * Button look: what the action means, not what colour it is.
 */
enum class ButtonVariant {
    PRIMARY,           // The main action of a screen (accent)
    PRIMARY_OUTLINE,   // The main action while it would have no visible effect: accent outline, still tappable
    SECONDARY,         // Other actions (raised surface)
    SECONDARY_OUTLINE, // A side action that shouldn't compete with the screen's own (border only)
    GHOST,             // Icon-only chrome such as a dialog's close (no border, tinted when pressed)
    DANGER,            // STOP and destructive actions only (red)
    DANGER_OUTLINE,    // STOP while there's nothing to stop: red outline, still tappable
};

/**
 * Options for createButton().
 */
struct ButtonOptions {
    const char* text = "";      // Label, may be empty for icon-only buttons
    const char* icon = nullptr; // Optional Icons:: constant shown before the text
    ButtonVariant variant = ButtonVariant::SECONDARY;
    int32_t height = 0;              // 0 = Theme::BUTTON_HEIGHT
    bool isCompact = false;          // Smaller look (BUTTON_HEIGHT_COMPACT, smaller type) for side actions; still a 64 px touch target
    lv_event_cb_t onClick = nullptr; // Click handler
    void* userData = nullptr;        // Passed to the handler as event user data
};

/**
 * Creates a themed button with a pressed and a disabled state.
 *
 * Width is left to the caller (layout or lv_obj_set_width); height defaults to the theme's button height.
 *
 * @param parent Parent object
 * @param options Text, icon, variant, height and click handler
 * @return The button
 */
lv_obj_t* createButton(lv_obj_t* parent, const ButtonOptions& options);

/**
 * Enables or disables a button (or any tappable object).
 *
 * @param button The button
 * @param isEnabled True to enable
 */
void setButtonEnabled(lv_obj_t* button, bool isEnabled);

/**
 * Changes a button's look after creation, e.g. STOP between filled and outline.
 *
 * @param button Button from createButton()
 * @param variant New variant
 */
void setButtonVariant(lv_obj_t* button, ButtonVariant variant);
