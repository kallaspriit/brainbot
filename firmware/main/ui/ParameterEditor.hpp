#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/UiContext.hpp"

/**
 * Opens the number pad for a parameter (PAR-004, PAR-007): its unit, decimals and range from the table, the current
 * value shown; OK asks the motor service for a run-time write. Values shown converted (position units, hex codes,
 * named values) are edited raw.
 *
 * @param context Motor service and navigation; outlives the pad
 * @param position Entry position in Parameters::ENTRIES
 * @param currentRaw The value as last read
 * @param title Pad title; nullptr for the entry's description
 */
void openParameterEditor(UiContext& context, size_t position, uint32_t currentRaw, const char* title = nullptr);
