#pragma once

#include "ui/UiContext.hpp"

/**
 * Opens the restore-defaults confirmation (D-025): what goes back to factory, what may be lost, and the power cycle
 * after. RESTORE asks the motor service to write 1011:01 = "load". Shared by the Faults screen and the Setup tab.
 *
 * @param context Motor service and navigation; outlives the dialog
 */
void openRestoreDefaultsConfirm(UiContext& context);

/**
 * Opens the confirmation for the blank-flash recovery (D-031): the table's values for every blank object, a fault
 * reset, then restore defaults.
 *
 * @param context UI context; the motor service runs the recovery
 */
void openRecoverConfirm(UiContext& context);
