#pragma once

#include <cstdint>

#include "lvgl.h"

/**
 * Creates a status badge: a rounded pill with an optional icon and a short text, tinted with one colour token.
 *
 * Empty until setBadge() is called.
 *
 * @param parent Parent object
 * @param isCompact Smaller text and padding, for lists of badges
 * @return The badge
 */
lv_obj_t* createBadge(lv_obj_t* parent, bool isCompact = false);

/**
 * Sets a badge's icon, text and colour.
 *
 * @param badge Badge from createBadge()
 * @param icon Icons:: constant, or nullptr for none
 * @param text Text to show
 * @param color Theme colour token (Theme::SUCCESS, Theme::WARNING…); the background is a faint tint of it
 */
void setBadge(lv_obj_t* badge, const char* icon, const char* text, uint32_t color);
