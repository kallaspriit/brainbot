#pragma once

#include <cstdint>

/**
 * CANopen object dictionary entries used with the NiMotion drives.
 *
 * Indices and sizes follow the NiMotion CANopen manual and a working PTU (pan-tilt unit) driver.
 */
struct ObjectEntry {
    uint16_t index;    // Object index
    uint8_t subIndex;  // Object sub-index
    uint8_t sizeBytes; // Size on the wire; 0 for strings, read with SdoClient::readString
};

namespace Objects {
// CiA 301
constexpr ObjectEntry HEARTBEAT_PRODUCER_MS = {0x1017, 0x00, 2};      // 0 = off; a heartbeat nobody acknowledges raises 0x7501 (bench 2026-10-11, D-023)
constexpr ObjectEntry COMMUNICATION_FAULT_ACTION = {0x200E, 0x09, 4}; // Reaction to 0x7501: response code in bits 31-24 (alarm table 9-1)
constexpr ObjectEntry ERROR_REGISTER = {0x1001, 0x00, 1};             // CiA 301 error register bits
constexpr ObjectEntry SAVE_PARAMETERS = {0x1010, 0x01, 4};            // Write SAVE_COMMAND_SAVE only; never sub-index 0A/0B (safety rules)
constexpr ObjectEntry RESTORE_DEFAULTS = {0x1011, 0x01, 4};           // Write RESTORE_COMMAND_LOAD only; never sub-index 0A/0B (safety rules)
constexpr ObjectEntry ALARM_HISTORY = {0x1003, 0x01, 4};              // First of 16 alarm words, newest first in :01-:10 (bench 2026-10-11)
constexpr ObjectEntry FAULT_REACTIONS = {0x200E, 0x01, 4};            // First of the alarm reaction words; 200E:00 holds the count
constexpr ObjectEntry STALL_REACTION = {0x200E, 0x02, 4};             // Stall (0x2312) reaction word
constexpr ObjectEntry DEVICE_NAME = {0x1008, 0x00, 0};                // VISIBLE_STRING, segmented upload
constexpr ObjectEntry HARDWARE_VERSION = {0x1009, 0x00, 0};           // VISIBLE_STRING, segmented upload
constexpr ObjectEntry SOFTWARE_VERSION = {0x100A, 0x00, 0};           // VISIBLE_STRING, segmented upload
constexpr ObjectEntry VENDOR_ID = {0x1018, 0x01, 4};
constexpr ObjectEntry PRODUCT_CODE = {0x1018, 0x02, 4};
constexpr ObjectEntry REVISION = {0x1018, 0x03, 4};
constexpr ObjectEntry SERIAL_NUMBER = {0x1018, 0x04, 4};

// NiMotion specific
constexpr ObjectEntry RATED_CURRENT = {0x2000, 0x09, 2}; // 0.01 A (manual object table, default 1250)
constexpr ObjectEntry POLE_PAIRS = {0x2000, 0x11, 2};    // Blank-flash sentinel (D-031); factory 4 on a BLM42
constexpr ObjectEntry ENCODER_BITS = {0x2000, 0x19, 4};  // Blank-flash sentinel (D-031); factory 18 on a BLM42
constexpr ObjectEntry RATED_VOLTAGE = {0x2000, 0x07, 2}; // 1 V, range 24-48 (CN manual); flash writes need the bus at about 80 % of it (bench 2026-10-11)
constexpr ObjectEntry RATED_SPEED = {0x2000, 0x0E, 2};   // rpm (manual object table, default 3000)
constexpr ObjectEntry STATOR_RESISTANCE = {0x2000, 0x12, 2};
constexpr ObjectEntry STATOR_INDUCTANCE_LQ = {0x2000, 0x13, 2};      // 0.01 mH; identified by the drive (Chinese manual §8.2)
constexpr ObjectEntry STATOR_INDUCTANCE_LD = {0x2000, 0x14, 2};      // 0.01 mH
constexpr ObjectEntry TORQUE_COEFFICIENT = {0x2000, 0x16, 2};        // Kt, 0.01 N·m/A; identified
constexpr ObjectEntry ROTOR_INERTIA = {0x2000, 0x10, 2};             // Jm, 0.01 kg·cm²; identified
constexpr ObjectEntry INITIAL_ANGLE = {0x2000, 0x1E, 2};             // Commutation offset, 65536 per electrical turn (inferred, bench 2026-10-15); 0 makes the identification measure it
constexpr ObjectEntry OPEN_LOOP_CURRENT = {0x2001, 0x1D, 2};         // 0.01 A; the identification locks the shaft at 75-100 % of it
constexpr ObjectEntry IDENTIFICATION_STAGE = {0x2009, 0x06, 2};      // 1 locking, 2 spinning, 3 done (manual figure 8-1); keeps its last value between runs
constexpr ObjectEntry VIRTUAL_INPUTS_ENABLED = {0x200C, 0x07, 2};    // 1 = the 2017h virtual inputs work (manual §7.1.1)
constexpr ObjectEntry VDI1_FUNCTION = {0x2017, 0x01, 2};             // Virtual input 1 function (table 7-2); 1 = servo enable, valid in NiMotion modes
constexpr ObjectEntry VDI1_LOGIC = {0x2017, 0x02, 2};                // 0 = active high
constexpr ObjectEntry VIRTUAL_INPUT_LEVELS = {0x2031, 0x01, 2};      // bit n = VDI(n+1) level, written over the bus         // 0.001 Ω (manual object table, default 115)
constexpr ObjectEntry BUS_UNDERVOLTAGE_LIMIT_V = {0x2001, 0x10, 2};  // 1 V, factory 24, takes effect when stopped; PTU sets 12
constexpr ObjectEntry CONTROL_MODE = {0x2002, 0x01, 2};              // 0 = CiA 402
constexpr ObjectEntry INPUT_FUNCTION_DI1 = {0x2003, 0x03, 2};        // DI1 function (table 7-2); DI2 at :05, DI3 at :07, logic in between
constexpr ObjectEntry INPUT_STATES = {0x200B, 0x05, 2};              // Input levels, bit0 = DI1
constexpr ObjectEntry HOMING_METHOD = {0x6098, 0x00, 1};             // The PTU requires 21; 24 on unconfigured motors (bench 2026-10-11)
constexpr ObjectEntry NODE_ID = {0x200C, 0x02, 2};                   // Takes effect after a power cycle; written only from the Setup danger group
constexpr ObjectEntry BITRATE = {0x200C, 0x03, 2};                   // Takes effect after a power cycle; 8 = 1 Mbit/s; written only from the Setup danger group
constexpr ObjectEntry COMMUNICATION_WATCHDOG_MS = {0x200C, 0x1B, 2}; // 0 = off; else alarm 0x7501 unless rewritten within this time (manual §watchdog)
constexpr ObjectEntry BUS_VOLTAGE = {0x200B, 0x15, 2};               // 0.1 V (the English manual swaps 15 and 16 in places)
constexpr ObjectEntry TEMPERATURE = {0x200B, 0x16, 2};               // Module temperature, °C

// CiA 402
constexpr ObjectEntry ERROR_CODE = {0x603F, 0x00, 2};
constexpr ObjectEntry CONTROLWORD = {0x6040, 0x00, 2};
constexpr ObjectEntry STATUSWORD = {0x6041, 0x00, 2};
constexpr ObjectEntry QUICK_STOP_OPTION_CODE = {0x605A, 0x00, 2}; // 2 = quick-stop ramp, then Switch On Disabled
constexpr ObjectEntry MODE_OF_OPERATION = {0x6060, 0x00, 1};
constexpr ObjectEntry POSITION_ACTUAL = {0x6063, 0x00, 4};             // Encoder counts
constexpr ObjectEntry POSITION_ACTUAL_USER = {0x6064, 0x00, 4};        // User units: 607A and 607D use these (CN:6537)
constexpr ObjectEntry FOLLOWING_ERROR_WINDOW = {0x6065, 0x00, 4};      // User units, default 50 (CN:6539)
constexpr ObjectEntry POSITION_WINDOW = {0x6067, 0x00, 4};             // User units, default 10; "target reached" band (CN:6554)
constexpr ObjectEntry TARGET_POSITION = {0x607A, 0x00, 4};             // User units, int32 (CN:6592)
constexpr ObjectEntry SOFTWARE_POSITION_LIMIT_MIN = {0x607D, 0x01, 4}; // User units, default -65535 (CN:6605)
constexpr ObjectEntry SOFTWARE_POSITION_LIMIT_MAX = {0x607D, 0x02, 4}; // User units, default 65535 (CN:6606)
constexpr ObjectEntry PROFILE_VELOCITY = {0x6081, 0x00, 4};            // User units/s, PP move speed (CN:6610)
constexpr ObjectEntry POSITIONING_OPTION_CODE = {0x60F2, 0x00, 2};     // Read only here; relative-move reference, unused (CN:6681)
constexpr ObjectEntry VELOCITY_ACTUAL = {0x606C, 0x00, 4};             // rpm (PTU code)
constexpr ObjectEntry CURRENT_ACTUAL = {0x6078, 0x00, 2};              // Motor-side current feedback, per mille of rated current 2000:09 (bench 2026-10-09; the CANopen manuals' 0.01 A is wrong)
constexpr ObjectEntry MAX_PROFILE_VELOCITY = {0x607F, 0x00, 4};        // User units/s
constexpr ObjectEntry MAX_MOTOR_SPEED = {0x6080, 0x00, 4};             // rpm (manual object table)
constexpr ObjectEntry PROFILE_ACCELERATION = {0x6083, 0x00, 4};        // User units/s²
constexpr ObjectEntry PROFILE_DECELERATION = {0x6084, 0x00, 4};        // User units/s²
constexpr ObjectEntry QUICK_STOP_DECELERATION = {0x6085, 0x00, 4};     // User units/s²
constexpr ObjectEntry MAX_ACCELERATION = {0x60C5, 0x00, 4};            // User units/s²; the drive caps 6083 at it (bench 2026-10-12)
constexpr ObjectEntry MAX_DECELERATION = {0x60C6, 0x00, 4};            // User units/s²; the drive caps 6084 at it (bench 2026-10-12)
constexpr ObjectEntry ENCODER_INCREMENTS = {0x608F, 0x01, 4};
constexpr ObjectEntry ENCODER_MOTOR_REVOLUTIONS = {0x608F, 0x02, 4};
constexpr ObjectEntry GEAR_MOTOR_REVOLUTIONS = {0x6091, 0x01, 4};
constexpr ObjectEntry GEAR_SHAFT_REVOLUTIONS = {0x6091, 0x02, 4};
constexpr ObjectEntry TARGET_VELOCITY = {0x60FF, 0x00, 4};            // User units/s
constexpr ObjectEntry TARGET_TORQUE = {0x6071, 0x00, 2};              // 0.1 % of rated torque, signed (§6.6)
constexpr ObjectEntry TORQUE_SLOPE = {0x6087, 0x00, 4};               // 0.1 % per second (§6.6)
constexpr ObjectEntry TORQUE_ACTUAL = {0x6077, 0x00, 2};              // 0.1 % of rated torque, signed
constexpr ObjectEntry RATED_TORQUE = {0x6076, 0x00, 4};               // 0.01 N·m; 8 on the BLM4203B (datasheet 0.083 N·m)
constexpr ObjectEntry TORQUE_SPEED_LIMIT_FORWARD = {0x2007, 0x10, 2}; // rpm; above it the drive holds the speed (§6.6.4)
constexpr ObjectEntry TORQUE_SPEED_LIMIT_REVERSE = {0x2007, 0x11, 2}; // rpm
} // namespace Objects

/**
 * Controlword commands for the CiA 402 state machine.
 */
namespace Controlword {
constexpr uint16_t DISABLE_VOLTAGE = 0x0000;       // → Switch On Disabled
constexpr uint16_t SHUTDOWN = 0x0006;              // → Ready To Switch On (power stage off)
constexpr uint16_t SWITCH_ON = 0x0007;             // → Switched On
constexpr uint16_t ENABLE_OPERATION = 0x000F;      // → Operation Enabled
constexpr uint16_t QUICK_STOP = 0x0002;            // Brake with 6085, then per 605A (D-008)
constexpr uint16_t PP_NEW_SET_POINT = 0x0010;      // Bit 4: rising edge sends 607A (profile position)
constexpr uint16_t PP_CHANGE_IMMEDIATELY = 0x0020; // Bit 5: the new target replaces the current move (CN:1394)
constexpr uint16_t HALT = 0x0100;                  // Bit 8: decelerate on 605D's ramp and hold (CN:1400)
constexpr uint16_t FAULT_RESET = 0x0080;           // Rising edge clears a fault
} // namespace Controlword

/**
 * Statusword bits used beyond the state machine (CiA 402; Chinese G03 manual CN:1419-1422).
 */
// CiA 301 restore command for 1011:01, "load" in ASCII (safety rules; manual §7.7.2). The NiMotion-area code 0x9A899E80
// is never used
constexpr uint32_t RESTORE_COMMAND_LOAD = 0x64616F6C;

// CiA 301 save command for 1010:01, "save" in ASCII (safety rules; manual §7.7.1). The NiMotion-area code 0x9A899E87
// is never used
constexpr uint32_t SAVE_COMMAND_SAVE = 0x65766173;

namespace StatuswordBit {
constexpr uint16_t FAULT = 0x0008;                 // Bit 3: the drive is in Fault or Fault Reaction Active
constexpr uint16_t WARNING = 0x0080;               // Bit 7: an alarm configured as a warning is active
constexpr uint16_t TARGET_REACHED = 0x0400;        // Bit 10: within 6067 of 607A (while halted: speed is 0)
constexpr uint16_t SET_POINT_ACKNOWLEDGE = 0x1000; // Bit 12 in PP: the drive took the new set-point
constexpr uint16_t FOLLOWING_ERROR = 0x2000;       // Bit 13 in PP: following error beyond 6065 for 6066
} // namespace StatuswordBit

/**
 * Modes of operation (object 6060).
 */
namespace OperationMode {
constexpr uint8_t PROFILE_POSITION = 1;
constexpr uint8_t PROFILE_VELOCITY = 3;
constexpr uint8_t PROFILE_TORQUE = 4; // Chinese manual §6.6 (CN:1822)
constexpr uint8_t HOMING = 6;
} // namespace OperationMode

// 2002:01 control mode (Chinese manual, 2002h table): 0 CiA 402, 1-3 NiMotion position, speed and torque, 4 open loop
namespace ControlMode {
constexpr uint16_t CIA402 = 0;
constexpr uint16_t IDENTIFICATION = 5; // Offline parameter identification (§8.2); enabled through a DI/VDI, not the controlword
} // namespace ControlMode

// Digital and virtual input functions (table 7-2)
constexpr uint16_t INPUT_FUNCTION_SERVO_ENABLE = 1; // Valid in the NiMotion modes only

/**
 * CiA 402 drive state, decoded from the statusword.
 */
enum class DriveState {
    NOT_READY_TO_SWITCH_ON,
    SWITCH_ON_DISABLED,
    READY_TO_SWITCH_ON,
    SWITCHED_ON,
    OPERATION_ENABLED,
    QUICK_STOP_ACTIVE,
    FAULT_REACTION_ACTIVE,
    FAULT,
    UNKNOWN,
};

/**
 * Decodes the CiA 402 state from a statusword.
 *
 * @param statusword Value of object 6041
 * @return The drive state
 */
inline DriveState decodeDriveState(uint16_t statusword) {
    if ((statusword & 0x4F) == 0x00) {
        return DriveState::NOT_READY_TO_SWITCH_ON;
    }

    if ((statusword & 0x4F) == 0x40) {
        return DriveState::SWITCH_ON_DISABLED;
    }

    if ((statusword & 0x6F) == 0x21) {
        return DriveState::READY_TO_SWITCH_ON;
    }

    if ((statusword & 0x6F) == 0x23) {
        return DriveState::SWITCHED_ON;
    }

    if ((statusword & 0x6F) == 0x27) {
        return DriveState::OPERATION_ENABLED;
    }

    if ((statusword & 0x6F) == 0x07) {
        return DriveState::QUICK_STOP_ACTIVE;
    }

    if ((statusword & 0x4F) == 0x0F) {
        return DriveState::FAULT_REACTION_ACTIVE;
    }

    if ((statusword & 0x4F) == 0x08) {
        return DriveState::FAULT;
    }

    return DriveState::UNKNOWN;
}

/**
 * Tells whether an alarm that halts the motor without a fault is active: the drive stays in Operation enabled with
 * neither the fault nor the warning bit set, and only 603F names the alarm. Reaction 3 in 200E (stall, software limit,
 * limit switch; a held shaft raised 0x2312 this way, bench 2026-10-11).
 *
 * @param statusword Value of object 6041
 * @param errorCode Value of object 603F
 * @return True if the drive is halted by an alarm
 */
inline bool isHaltAlarm(uint16_t statusword, uint16_t errorCode) {
    bool hasBits = (statusword & (StatuswordBit::FAULT | StatuswordBit::WARNING)) != 0;

    return errorCode != 0 && !hasBits && decodeDriveState(statusword) == DriveState::OPERATION_ENABLED;
}

/**
 * Tells whether the drive reports an active alarm: the fault or warning bit, or an alarm that halts the motor. An error
 * code in another state without either bit is a leftover, such as the 0x7501 the drive shows for about a second after
 * power-on in Ready To Switch On (bench 2026-10-11).
 *
 * @param statusword Value of object 6041
 * @param errorCode Value of object 603F
 * @return True if an alarm is active
 */
inline bool isAlarmActive(uint16_t statusword, uint16_t errorCode) {
    bool hasBits = (statusword & (StatuswordBit::FAULT | StatuswordBit::WARNING)) != 0;

    return hasBits || isHaltAlarm(statusword, errorCode);
}

/**
 * Returns a readable name for a drive state.
 *
 * @param state The drive state
 * @return Name of the state
 */
inline const char* getDriveStateName(DriveState state) {
    switch (state) {
        case DriveState::NOT_READY_TO_SWITCH_ON:
            return "Not ready to switch on";

        case DriveState::SWITCH_ON_DISABLED:
            return "Switch on disabled";

        case DriveState::READY_TO_SWITCH_ON:
            return "Ready to switch on";

        case DriveState::SWITCHED_ON:
            return "Switched on";

        case DriveState::OPERATION_ENABLED:
            return "Operation enabled";

        case DriveState::QUICK_STOP_ACTIVE:
            return "Quick stop active";

        case DriveState::FAULT_REACTION_ACTIVE:
            return "Fault reaction active";

        case DriveState::FAULT:
            return "Fault";

        case DriveState::UNKNOWN:
            return "Unknown";
    }

    return "Unknown";
}
