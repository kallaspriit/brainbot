#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates the Parameters tab's list (PAR-003): every parameter in the table by group, read from the drive when the list
 * opens and whenever a drive connects. Groups start collapsed and show how many of their values differ from factory;
 * a tapped group opens. Each row shows the value in its unit, the factory value when different, and marks for the
 * console's own values and locked parameters. A tapped row opens its details. Read-only for now (PAR-004 adds editing).
 *
 * Fills its parent's free height and scrolls. Keeps a copy of the values, freed with the list.
 *
 * @param parent Parent object (a flex column)
 * @param context Motor service, state and navigation; outlives the list
 * @return The list's container
 */
lv_obj_t* createParameterList(lv_obj_t* parent, UiContext& context);
