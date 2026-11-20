#pragma once

#include <cstdint>

/**
 * A NiMotion alarm, by the 16-bit error code the drive reports in 603F and EMCY.
 */
struct AlarmCode {
    uint16_t code;         // 603F value, the low 16 bits of the alarm code
    const char* shortName; // One or two words, for badges
    const char* name;      // Name from the alarm table
};

/**
 * NiMotion alarm names.
 *
 * Source: the alarm table (Table 9-1) of the English NiMotion CANopen manual. Names are unverified against the Chinese manual (the Faults plan does that); short names are ours.
 */
namespace AlarmCodes {
// Codes the console reacts to
constexpr uint16_t STALL = 0x2312;        // Factory reaction halts and holds at up to the rated current (bench 2026-10-11)
constexpr uint16_t UNDERVOLTAGE = 0x3220; // Also raised by every power-off as the supply falls (bench 2026-10-11)
constexpr uint16_t OVERTEMPERATURE = 0x4210;
constexpr uint16_t UNDERTEMPERATURE = 0x4220;
constexpr uint16_t COMMUNICATION_FAILURE = 0x7501; // Also raised while the drive writes its flash (bench 2026-10-11)
constexpr uint16_t SAVE_FAULT = 0xFF02;            // Flash write refused; at 18 V supply, not at 24 V (bench 2026-10-11)

/**
 * Tells whether an alarm only goes away with a motor power cycle: the drive, gate, flash and hardware initialisation
 * faults (manual table 9-1 note: "after a drive fault or hardware initialization failure, the integrated servo motor can
 * only operate normally after a restart").
 *
 * @param code Error code from 603F
 * @return True for those alarms
 */
bool needsPowerCycle(uint16_t code);

/**
 * Looks up an alarm by its error code.
 *
 * @param code Error code from 603F or an EMCY message
 * @return The alarm, or nullptr if the code isn't in the table
 */
const AlarmCode* find(uint16_t code);
} // namespace AlarmCodes
