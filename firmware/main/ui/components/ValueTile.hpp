#pragma once

#include <cstdint>

#include "lvgl.h"

/**
 * Options for createValueTile().
 */
struct ValueTileOptions {
    const char* label = "";                 // Name above the value
    const char* unit = "";                  // Unit after the value
    lv_subject_t* subject = nullptr;        // Integer subject holding the value
    uint8_t decimals = 0;                   // Fixed-point decimals of the subject (1 for 0.1 V)
    lv_subject_t* warningSubject = nullptr; // Optional: the value turns amber while this is non-zero
    bool isLarge = false;                   // Larger number, for the main value of a screen
};

/**
 * Creates a value tile: a label, a large fixed-width number bound to a subject, and its unit.
 *
 * Size is left to the caller (flex grow or lv_obj_set_size).
 *
 * @param parent Parent object
 * @param options Label, unit, subject and warning subject
 * @return The tile
 */
lv_obj_t* createValueTile(lv_obj_t* parent, const ValueTileOptions& options);
