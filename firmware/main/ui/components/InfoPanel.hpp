#pragma once

#include <cstdint>

#include "lvgl.h"

/**
 * Creates an information section: a heading over rows added with addInfoRow(), with no box around it (UI design:
 * surfaces for tiles and controls, flat for reports). Separate sections with createDivider().
 *
 * Size is left to the caller (flex grow or lv_obj_set_size).
 *
 * @param parent Parent object
 * @param title Section heading
 * @return The section
 */
lv_obj_t* createInfoPanel(lv_obj_t* parent, const char* title);

/**
 * Creates a 1 px vertical divider in the border colour, as tall as its parent, for between flat sections in a row.
 *
 * @param parent Row of sections
 * @return The divider
 */
lv_obj_t* createDivider(lv_obj_t* parent);

/**
 * Adds a "name  value" row to an information panel and returns the value label, for binding. Long values wrap.
 *
 * @param panel Panel from createInfoPanel()
 * @param name Row name
 * @param nameWidth Width of the name column, the same for every row of a panel
 * @return The value label
 */
lv_obj_t* addInfoRow(lv_obj_t* panel, const char* name, int32_t nameWidth);
