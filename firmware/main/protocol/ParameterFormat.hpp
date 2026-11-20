#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol/ParameterTable.hpp"

/**
 * Turns parameter values into text for the Parameters screen (PAR-003): sign by type, scale by the manual's unit,
 * value names for enumerations, and position units in turns.
 */
namespace ParameterFormat {
/**
 * Returns a raw value as the entry's type: sign-extended for signed types.
 *
 * @param entry Table entry
 * @param raw Value as read
 * @return The value
 */
int64_t toValue(const Parameters::Entry& entry, uint32_t raw);

/**
 * Formats a value for reading: "12 V", "30.0 Hz", "CiA 402", "1.250 turns", "0x0401FF02".
 *
 * @param entry Table entry
 * @param raw Value as read
 * @param unitsPerTurn User units per motor turn (MotorStatus::unitsPerTurn), 0 if unknown
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
void formatValue(const Parameters::Entry& entry, uint32_t raw, int32_t unitsPerTurn, char* buffer, size_t bufferSize);

/**
 * Formats a value as the drive holds it, for beside the converted value: decimal, or hex for codes.
 *
 * @param entry Table entry
 * @param raw Value as read
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
void formatRaw(const Parameters::Entry& entry, uint32_t raw, char* buffer, size_t bufferSize);

/**
 * How a value in the drive's position units is shown and edited: turns, rpm or rpm/s, as fixed point.
 */
struct UserUnitScale {
    const char* unit = "";  // "turns", "rpm" or "rpm/s"
    uint8_t decimals = 0;   // Display values are in units of 10^-decimals
    int64_t multiplier = 1; // Display value = raw * multiplier / unitsPerTurn
};

/**
 * Tells how an entry in the drive's position units is shown and edited (PAR-004: edit in the shown unit, not raw).
 *
 * @param entry Table entry
 * @param unitsPerTurn 6064 user units per motor turn; 0 if unknown
 * @param scale Receives the unit, decimals and multiplier
 * @return False if the entry isn't in position units or the units per turn are unknown
 */
bool getUserUnitScale(const Parameters::Entry& entry, int32_t unitsPerTurn, UserUnitScale& scale);

/**
 * Converts a raw value in position units to its display value.
 *
 * @param value Raw value, sign-extended
 * @param scale From getUserUnitScale()
 * @param unitsPerTurn User units per motor turn, above 0
 * @return Display value in units of 10^-decimals
 */
int64_t toDisplayValue(int64_t value, const UserUnitScale& scale, int32_t unitsPerTurn);

/**
 * Converts a display value back to the raw value, rounded to the nearest unit.
 *
 * @param display Display value in units of 10^-decimals
 * @param scale From getUserUnitScale()
 * @param unitsPerTurn User units per motor turn, above 0
 * @return Raw value
 */
int64_t fromDisplayValue(int64_t display, const UserUnitScale& scale, int32_t unitsPerTurn);

/**
 * Tells whether the formatted value differs from its raw form, so the raw one is worth showing too.
 *
 * @param entry Table entry
 * @return True for scaled values, named values, position units and codes shown in hex
 */
bool isConverted(const Parameters::Entry& entry);

/**
 * Tells whether a value differs from the drive's factory value.
 *
 * @param entry Table entry
 * @param raw Value as read
 * @return True if the entry has a factory value and the value differs
 */
bool differsFromFactory(const Parameters::Entry& entry, uint32_t raw);

/**
 * Tells whether an entry's range is just its data type's limits (the manual gives 0~65535 for many uint16 entries).
 *
 * @param entry Table entry
 * @return True if the range adds nothing to the type
 */
bool isFullTypeRange(const Parameters::Entry& entry);

/**
 * Tells whether the console lets a user change an entry (D-028, D-029): writable, and not locked, Setup-only, a
 * console session value or a value the motion screens write.
 *
 * @param entry Table entry
 * @return True if editable
 */
bool isEditable(const Parameters::Entry& entry);

/**
 * Tells whether an entry may be changed while the motor runs (D5 of the Parameters plan): only those that take effect
 * immediately, such as the gains.
 *
 * @param entry Table entry
 * @return True if a change applies at once
 */
bool isEditableWhileRunning(const Parameters::Entry& entry);

/**
 * Returns an entry's size on the wire.
 *
 * @param entry Table entry
 * @return 1, 2 or 4 bytes
 */
uint8_t getSizeBytes(const Parameters::Entry& entry);

/**
 * Describes when a written value takes effect.
 *
 * @param effect From the table
 * @return Text such as "immediately"
 */
const char* getEffectText(Parameters::Effect effect);

/**
 * Names a data type.
 *
 * @param type From the table
 * @return Text such as "uint16"
 */
const char* getTypeText(Parameters::Type type);
} // namespace ParameterFormat
