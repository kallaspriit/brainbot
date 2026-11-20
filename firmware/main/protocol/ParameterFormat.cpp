#include "ParameterFormat.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace ParameterFormat {
// Objects that hold codes or bit fields rather than quantities: shown in hex
static bool isHexObject(uint16_t index) {
    bool isCode = index == 0x1000 || index == 0x1001 || index == 0x1003 || index == 0x200E || index == 0x603F;
    bool isControlOrStatus = index == 0x6040 || index == 0x6041;
    bool isPdoSetUp = index >= 0x1400 && index <= 0x1A03;

    return isCode || isControlOrStatus || isPdoSetUp;
}

// Position units: the drive's user units, which 608F/6091 relate to motor turns
static bool isUserUnit(const Parameters::Entry& entry) {
    return strncmp(entry.unit, "user units", 10) == 0;
}

int64_t toValue(const Parameters::Entry& entry, uint32_t raw) {
    switch (entry.type) {
        case Parameters::Type::INT8:
            return static_cast<int8_t>(raw);

        case Parameters::Type::INT16:
            return static_cast<int16_t>(raw);

        case Parameters::Type::INT32:
            return static_cast<int32_t>(raw);

        default:
            return raw;
    }
}

/**
 * Formats a value with a number of decimals: 1234 with 2 decimals is "12.34".
 *
 * @param value Value in the smallest unit
 * @param decimals Digits after the point
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
static void formatScaled(int64_t value, uint8_t decimals, char* buffer, size_t bufferSize) {
    if (decimals == 0) {
        snprintf(buffer, bufferSize, "%" PRId64, value);

        return;
    }

    int64_t divisor = 1;

    for (uint8_t digit = 0; digit < decimals; digit++) {
        divisor *= 10;
    }

    const char* sign = value < 0 ? "-" : "";
    int64_t magnitude = value < 0 ? -value : value;
    snprintf(buffer, bufferSize, "%s%" PRId64 ".%0*" PRId64, sign, magnitude / divisor, static_cast<int>(decimals), magnitude % divisor);
}

void formatValue(const Parameters::Entry& entry, uint32_t raw, int32_t unitsPerTurn, char* buffer, size_t bufferSize) {
    int64_t value = toValue(entry, raw);

    // Named values first: a mode reads better as its name
    for (size_t label = 0; label < entry.labelCount; label++) {
        if (entry.labels[label].value == value) {
            snprintf(buffer, bufferSize, "%s", entry.labels[label].label);

            return;
        }
    }

    if (isHexObject(entry.index)) {
        snprintf(buffer, bufferSize, "0x%0*" PRIX32, entry.type == Parameters::Type::UINT32 || entry.type == Parameters::Type::INT32 ? 8 : 4, raw);

        return;
    }

    // Position units as turns, their rates as rpm and rpm/s, like the Position screen
    if (isUserUnit(entry) && unitsPerTurn > 0) {
        const char* rate = entry.unit + 10;

        int64_t magnitude = value < 0 ? -value : value;

        if (rate[0] == '\0' && magnitude * 10 < unitsPerTurn) {
            // Under a tenth of a turn (windows, small offsets): turns would round to nothing, so units and degrees
            snprintf(buffer, bufferSize, "%" PRId64 " units (", value);
            size_t length = strlen(buffer);
            formatScaled(value * 3600 / unitsPerTurn, 1, buffer + length, bufferSize - length);
            strncat(buffer, "°)", bufferSize - strlen(buffer) - 1);
        } else if (rate[0] == '\0') {
            formatScaled(value * 1000 / unitsPerTurn, 3, buffer, bufferSize);
            strncat(buffer, " turns", bufferSize - strlen(buffer) - 1);
        } else if (strcmp(rate, "/s") == 0) {
            // Tenths only when there are any: 6000 rpm, 30.5 rpm
            int64_t tenthsRpm = value * 600 / unitsPerTurn;

            if (tenthsRpm % 10 == 0) {
                formatScaled(tenthsRpm / 10, 0, buffer, bufferSize);
            } else {
                formatScaled(tenthsRpm, 1, buffer, bufferSize);
            }

            strncat(buffer, " rpm", bufferSize - strlen(buffer) - 1);
        } else {
            formatScaled(value * 60 / unitsPerTurn, 0, buffer, bufferSize);
            strncat(buffer, " rpm/s", bufferSize - strlen(buffer) - 1);
        }

        return;
    }

    formatScaled(value, entry.decimals, buffer, bufferSize);

    if (entry.unit[0] != '\0') {
        strncat(buffer, " ", bufferSize - strlen(buffer) - 1);
        strncat(buffer, entry.unit, bufferSize - strlen(buffer) - 1);
    }
}

void formatRaw(const Parameters::Entry& entry, uint32_t raw, char* buffer, size_t bufferSize) {
    if (isHexObject(entry.index)) {
        snprintf(buffer, bufferSize, "%" PRIu32, raw);

        return;
    }

    snprintf(buffer, bufferSize, "%" PRId64, toValue(entry, raw));
}

bool getUserUnitScale(const Parameters::Entry& entry, int32_t unitsPerTurn, UserUnitScale& scale) {
    if (!isUserUnit(entry) || unitsPerTurn <= 0) {
        return false;
    }

    // The same units formatValue shows: positions in turns, rates in rpm (tenths), accelerations in rpm/s
    const char* rate = entry.unit + 10;

    if (rate[0] == '\0') {
        scale = {"turns", 3, 1000};
    } else if (strcmp(rate, "/s") == 0) {
        scale = {"rpm", 1, 600};
    } else {
        scale = {"rpm/s", 0, 60};
    }

    return true;
}

int64_t toDisplayValue(int64_t value, const UserUnitScale& scale, int32_t unitsPerTurn) {
    return value * scale.multiplier / unitsPerTurn;
}

int64_t fromDisplayValue(int64_t display, const UserUnitScale& scale, int32_t unitsPerTurn) {
    // Rounded, so 6000 rpm/s comes back as exactly what it was shown from
    int64_t numerator = display * unitsPerTurn;
    int64_t half = scale.multiplier / 2;

    return numerator >= 0 ? (numerator + half) / scale.multiplier : (numerator - half) / scale.multiplier;
}

bool isConverted(const Parameters::Entry& entry) {
    return entry.decimals > 0 || entry.labelCount > 0 || isUserUnit(entry) || isHexObject(entry.index);
}

bool differsFromFactory(const Parameters::Entry& entry, uint32_t raw) {
    return entry.hasFactory && toValue(entry, raw) != entry.factory;
}

bool isFullTypeRange(const Parameters::Entry& entry) {
    int64_t minimum = 0;
    int64_t maximum = 0;

    switch (entry.type) {
        case Parameters::Type::UINT8:
            maximum = UINT8_MAX;
            break;

        case Parameters::Type::INT8:
            minimum = INT8_MIN;
            maximum = INT8_MAX;
            break;

        case Parameters::Type::UINT16:
            maximum = UINT16_MAX;
            break;

        case Parameters::Type::INT16:
            minimum = INT16_MIN;
            maximum = INT16_MAX;
            break;

        case Parameters::Type::UINT32:
            maximum = UINT32_MAX;
            break;

        case Parameters::Type::INT32:
            minimum = INT32_MIN;
            maximum = INT32_MAX;
            break;

        default:
            return false;
    }

    return entry.rangeMin == minimum && entry.rangeMax == maximum;
}

bool isEditable(const Parameters::Entry& entry) {
    uint8_t notEditable = Parameters::Flag::LOCKED | Parameters::Flag::SETUP_ONLY | Parameters::Flag::CONSOLE_OWNED | Parameters::Flag::SESSION;

    return entry.access == Parameters::Access::READ_WRITE && (entry.flags & notEditable) == 0;
}

bool isEditableWhileRunning(const Parameters::Entry& entry) {
    return entry.effect == Parameters::Effect::IMMEDIATE;
}

uint8_t getSizeBytes(const Parameters::Entry& entry) {
    switch (entry.type) {
        case Parameters::Type::UINT8:
        case Parameters::Type::INT8:
            return 1;

        case Parameters::Type::UINT16:
        case Parameters::Type::INT16:
            return 2;

        default:
            return 4;
    }
}

const char* getEffectText(Parameters::Effect effect) {
    switch (effect) {
        case Parameters::Effect::IMMEDIATE:
            return "immediately";

        case Parameters::Effect::WHEN_STOPPED:
            return "when the motor stops";

        case Parameters::Effect::AFTER_POWER_UP:
            return "after a motor power cycle";

        case Parameters::Effect::NONE:
            break;
    }

    return "not stated";
}

const char* getTypeText(Parameters::Type type) {
    switch (type) {
        case Parameters::Type::UINT8:
            return "uint8";

        case Parameters::Type::INT8:
            return "int8";

        case Parameters::Type::UINT16:
            return "uint16";

        case Parameters::Type::INT16:
            return "int16";

        case Parameters::Type::UINT32:
            return "uint32";

        case Parameters::Type::INT32:
            return "int32";

        case Parameters::Type::STRING:
            return "string";

        case Parameters::Type::UNKNOWN:
            break;
    }

    return "unknown";
}
} // namespace ParameterFormat
