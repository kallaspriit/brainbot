#pragma once

#include "lvgl.h"

/**
 * Shows the start-up splash over everything: "SPARKY" fades in, hold, fade out, then the
 * background fades away to reveal the screen built underneath. A tap skips to the end. Deletes itself when done.
 *
 * Call with the display lock held, after the first screen is built.
 *
 * @param shouldHold Stop after the fade-in and stay until tapped (the `splash` serial command, for screenshots)
 */
void showSplash(bool shouldHold = false);
