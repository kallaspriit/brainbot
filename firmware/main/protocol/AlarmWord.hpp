#pragma once

#include <cstddef>
#include <cstdint>

/**
 * A NiMotion 32-bit alarm word: how the drive reacts to an alarm (200E) and what it records in its alarm history (1003).
 *
 * Manual §9.2: response code in bits 31-24, self-reset permission in bits 23-16, alarm code in bits 15-0. The history
 * entries use the same format (bench 2026-10-11, docs/NiMotion motors.md).
 */
namespace AlarmWord {
constexpr uint8_t RESPONSE_HALT_AND_HOLD = 3; // §9.2: halts and holds, no fault

/**
 * Returns the alarm code (the 603F value) of an alarm word.
 *
 * @param word Alarm word
 * @return Low 16 bits
 */
constexpr uint16_t getCode(uint32_t word) {
    return static_cast<uint16_t>(word & 0xFFFF);
}

/**
 * Returns the response code of an alarm word: 0-2 fault and stop, 3 halt and hold, 4 warning, 5-6 ignored.
 *
 * @param word Alarm word
 * @return Bits 31-24
 */
constexpr uint8_t getResponseCode(uint32_t word) {
    return static_cast<uint8_t>(word >> 24);
}

/**
 * Tells whether the drive clears this alarm by itself once its cause is gone.
 *
 * @param word Alarm word
 * @return True if the self-reset permission (bits 23-16) is set
 */
constexpr bool isSelfReset(uint32_t word) {
    return ((word >> 16) & 0xFF) != 0;
}

/**
 * Describes a response code in a few words.
 *
 * @param responseCode Bits 31-24 of an alarm word
 * @return Text such as "fault, stops on the quick ramp"
 */
const char* getResponseText(uint8_t responseCode);

/**
 * Names a response code in one word, for lists.
 *
 * @param responseCode Bits 31-24 of an alarm word
 * @return "fault", "halt", "warning" or "ignored"
 */
const char* getResponseShortText(uint8_t responseCode);

/**
 * Describes the whole reaction of an alarm word: the response and whether it resets itself.
 *
 * @param word Alarm word
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
void formatReaction(uint32_t word, char* buffer, size_t bufferSize);

/**
 * Finds the factory reaction for a configurable alarm (200E:01-:11), as a restore leaves it on firmware V0.2.5.a5
 * (FLT-007): the manual's table 9-2, except communication failure, which is halt and hold rather than fault.
 *
 * @param code Alarm code
 * @param word Receives the default alarm word
 * @return True if the alarm is in the table
 */
bool findFactoryDefault(uint16_t code, uint32_t& word);

/**
 * Lists the CiA 301 error register (1001) bits that are set.
 *
 * @param errorRegister Value of 1001
 * @param buffer Receives the text, e.g. "generic, voltage", or "none"
 * @param bufferSize Size of the buffer
 */
void formatErrorRegister(uint8_t errorRegister, char* buffer, size_t bufferSize);
} // namespace AlarmWord
