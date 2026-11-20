#include "AlarmCodes.hpp"

// Names follow the Chinese manual (G03) table 9-1, which lists the codes this drive family raises; the English
// manual's table has more codes and some mistranslations. "Bench" marks a code seen on a real drive (FLT-009).
// Codes marked * in the manual can't be configured through 200E
// clang-format off
static const AlarmCode ALARMS[] = {
    {0x2300, "Overcurrent",       "Motor overcurrent"},                   // Bench 2026-10-11, failed motor
    {0x2311, "Overload",          "Motor overload"},
    {0x2312, "Stall",             "Motor stalled"},                       // Bench 2026-10-11, shaft held at 600 rpm: halts and holds
    {0x2321, "Phase A short",     "Phase A upper bridge arm short circuit"},
    {0x2322, "Phase A short",     "Phase A lower bridge arm short circuit"},
    {0x2323, "Phase B short",     "Phase B upper bridge arm short circuit"},
    {0x2324, "Phase B short",     "Phase B lower bridge arm short circuit"},
    {0x2325, "Phase C short",     "Phase C upper bridge arm short circuit"},
    {0x2326, "Phase C short",     "Phase C lower bridge arm short circuit"},
    {0x3210, "Overvoltage",       "Supply overvoltage"},
    {0x3220, "Undervoltage",      "Supply undervoltage"},                 // Bench 2026-10-11, every power-off
    {0x3285, "Phase loss",        "Output phase failure"},                // Table 9-2 only
    {0x4210, "Overtemperature",   "High temperature alarm"},
    {0x4220, "Undertemperature",  "Low temperature alarm"},
    {0x5080, "Driver fault",      "Drive failure"},
    {0x5081, "Phase A gate",      "Phase A upper bridge arm gate failure"},
    {0x5082, "Phase A gate",      "Phase A lower bridge arm gate failure"},
    {0x5083, "Phase B gate",      "Phase B upper bridge arm gate failure"},
    {0x5084, "Phase B gate",      "Phase B lower bridge arm gate failure"},
    {0x5085, "Phase C gate",      "Phase C upper bridge arm gate failure"},
    {0x5086, "Phase C gate",      "Phase C lower bridge arm gate failure"},
    {0x5540, "Flash init",        "Flash initialization failure"},
    {0x5541, "Flash init",        "Flash initialization failure"},
    {0x5542, "Flash check",       "Flash validation error"},
    {0x5543, "No parameters",     "No parameters in the flash user area"},
    {0x6000, "Hardware init",     "Hardware initialization failure"},
    {0x6320, "Bad parameter",     "Parameter setting error"},
    {0x6321, "Registration",      "Registration failure"},
    {0x7306, "Encoder failure",   "Encoder failure"},
    {0x7307, "Encoder warning",   "Encoder warning"},
    {0x7308, "Encoder cal",       "Encoder calibration failed or no calibration data in flash"},
    {0x7310, "Overspeed",         "Speed above the limit"},
    {0x7501, "Comms timeout",     "Communication failure"},               // Bench 2026-10-11, heartbeat lost and after a flash write
    {0x8610, "Homing timeout",    "Homing timeout"},
    {0x8611, "Following error",   "Position error beyond the limit"},
    {0x8613, "Soft limit",        "Software position limit"},
    {0x8614, "Limit switch",      "Limit switch"},
    {0x8615, "Profile error",     "Motion profile calculation error"},
    {0x8616, "Target overflow",   "Target position overflow"},
    {0x8617, "Profile too small", "Motion profile parameters too small"},
    {0xFF01, "Motor ID failed",   "Motor parameter identification failure"},
    {0xFF02, "Save fault",        "Parameter saving fault"},              // Bench 2026-10-11, save or restore below 80 % of 2000:07

    // Only in the English manual's table; kept so these never show without a name
    {0x2301, "Overcurrent",       "Overcurrent"},
    {0x3211, "Brake resistor",    "Overvoltage relief fault"},            // "Pressure relief" read as the braking resistor; unverified
    {0x5544, "Position store",    "Position stored at power-down is abnormal"},
    {0x5545, "Data not stored",   "Data for power-down was not stored"},
    {0x7301, "Encoder battery",   "Encoder battery voltage low (fault)"},
    {0x7302, "Encoder battery",   "Encoder battery voltage low (alarm)"},
    {0x7303, "Encoder comms",     "Encoder communication fault"},
    {0x7304, "Multi-turn count",  "Encoder multi-turn counting error"},
    {0x7305, "Z pulse",           "Z pulse failure"},
    {0x7309, "Encoder internal",  "Internal encoder fault"},
    {0x8700, "Following error",   "Tracking fault"},
    {0xFF0C, "Encoder cal",       "Encoder calibration timeout"},

    // Only in the BLM57 motor manual's alarm table (BLM series); the 75xx are RS485 (Modbus) errors
    {0x8311, "Torque limit",      "Torque-limiting protection"},          // Bench 2026-10-12, held stall with the stall reaction set to warning
    {0x7502, "Illegal address",   "Illegal address (RS485)"},
    {0x7503, "Illegal data",      "Illegal data value (RS485)"},
    {0x7505, "Comms confirm",     "Confirmation in communication (RS485)"},
    {0x7506, "Slave busy",        "Slave in communication (RS485)"},
    {0x750C, "Request too large", "Request data is larger than mapping data (RS485)"},
    {0x750D, "Request mismatch",  "Request data count doesn't match the mapping (RS485)"},
    {0x750E, "Packet address",    "Packet node address is incorrect (RS485)"},
};
// clang-format on

namespace AlarmCodes {
bool needsPowerCycle(uint16_t code) {
    bool isBridgeShort = code >= 0x2321 && code <= 0x2326;
    bool isDriveOrGate = code >= 0x5080 && code <= 0x5086;
    bool isFlash = code >= 0x5540 && code <= 0x5543;

    return isBridgeShort || isDriveOrGate || isFlash || code == 0x6000;
}

const AlarmCode* find(uint16_t code) {
    for (const AlarmCode& alarm : ALARMS) {
        if (alarm.code == code) {
            return &alarm;
        }
    }

    return nullptr;
}
} // namespace AlarmCodes
