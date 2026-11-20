#pragma once

#include "ui/UiContext.hpp"

/**
 * Opens a read-only dialog listing the drive's configurable fault reactions (200E): for each alarm, what the drive does
 * and whether it resets itself, coloured by severity, with the rows that differ from the manual's defaults marked
 * (Faults plan, D4). Shown from the Faults screen; Parameters can open it too.
 *
 * @param context Motor for the fault record, navigator for the overlay layer
 */
void openFaultReactionsDialog(UiContext& context);
