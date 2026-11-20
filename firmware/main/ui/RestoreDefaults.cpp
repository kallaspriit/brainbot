#include "RestoreDefaults.hpp"

#include "services/MotorService.hpp"
#include "ui/Navigator.hpp"
#include "ui/components/ConfirmDialog.hpp"

// What the restore confirmation says (D-025)
static const char* const RESTORE_POINTS[] = {
    "Every setting goes back to the factory value: gains, current limits, fault reactions, inputs, and node ID and bitrate if the drive restores those.",
    "The drive's alarm history may be cleared too: take a photo of it first if it matters.",
    "Power-cycle the motor afterwards, so settings that load at start-up take effect too.",
    "A motor going back into a PTU needs \"setup pantilt\" on the PTU console.",
};

static void onRestoreConfirmed(void* userData) {
    static_cast<MotorService*>(userData)->requestRestoreDefaults();
}

void openRestoreDefaultsConfirm(UiContext& context) {
    ConfirmDialogOptions options;
    options.title = "Restore factory defaults?";
    options.subtitle = "For a motor that misbehaves because of its settings.";
    options.points = RESTORE_POINTS;
    options.pointCount = sizeof(RESTORE_POINTS) / sizeof(RESTORE_POINTS[0]);
    options.writes = "1011:01 = 0x64616F6C (load)";
    options.confirmText = "RESTORE";
    options.onConfirm = onRestoreConfirmed;
    options.userData = &context.motor;
    openConfirmDialog(context.navigator.getOverlayLayer(), options);
}

// What the recovery confirmation says (D-031)
static const char* const RECOVER_POINTS[] = {
    "Every setting that reads 0xFFFF gets the console's table value first. Those are a BLM42's factory values and are run-time only.",
    "Then the fault is reset and factory defaults are restored, which replaces them with this drive's own values. Until that restore succeeds, SAVE is refused.",
    "A write the drive refuses is skipped and logged; a failed reset or restore stops the sequence.",
    "Power-cycle the motor afterwards, so settings that load at start-up take effect too.",
};

static void onRecoverConfirmed(void* userData) {
    static_cast<MotorService*>(userData)->requestRecover();
}

void openRecoverConfirm(UiContext& context) {
    ConfirmDialogOptions options;
    options.title = "Recover the blank parameter flash?";
    options.subtitle = "For a motor whose stored settings all read 0xFFFF and that refuses to save.";
    options.points = RECOVER_POINTS;
    options.pointCount = sizeof(RECOVER_POINTS) / sizeof(RECOVER_POINTS[0]);
    options.writes = "blank objects = table values, 6040 = 0x80, 1011:01 = 0x64616F6C (load)";
    options.confirmText = "RECOVER";
    options.onConfirm = onRecoverConfirmed;
    options.userData = &context.motor;
    openConfirmDialog(context.navigator.getOverlayLayer(), options);
}
