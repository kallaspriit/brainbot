#include "AlarmWord.hpp"

#include <cstdio>
#include <cstring>

namespace AlarmWord {
const char* getResponseText(uint8_t responseCode) {
    // Manual §9.2, "Specific definition of fault response code"; 605E is the fault reaction option code
    switch (responseCode) {
        case 0:
            return "fault, stops as 605E says";

        case 1:
            return "fault, stops on the slow ramp";

        case 2:
            return "fault, stops on the quick ramp";

        case 3:
            return "halts and holds, no fault";

        case 4:
            return "warning only";

        case 5:
            return "ignored, recorded";

        case 6:
            return "ignored";
    }

    return "unknown response";
}

const char* getResponseShortText(uint8_t responseCode) {
    if (responseCode <= 2) {
        return "fault";
    }

    if (responseCode == 3) {
        return "halt";
    }

    if (responseCode == 4) {
        return "warning";
    }

    return "ignored";
}

void formatReaction(uint32_t word, char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "%s, %s", getResponseText(getResponseCode(word)), isSelfReset(word) ? "resets itself" : "needs a reset");
}

bool findFactoryDefault(uint16_t code, uint32_t& word) {
    // Firmware V0.2.5.a5 after a restore (FLT-007): the manual's table 9-2, except communication failure, which the
    // manual gives as 0x00017501. In 200E sub-index order
    static constexpr uint32_t DEFAULTS[] = {
        0x04012311, // Motor overload
        0x03002312, // Motor stalled
        0x00013210, // Power overvoltage
        0x01013220, // Power undervoltage
        0x00014210, // High temperature
        0x00014220, // Low temperature
        0x00016320, // Parameter setting error
        0x00017310, // Speeding
        0x03017501, // Communication failure: halt and hold, resets itself
        0x00008610, // Homing timeout
        0x00008611, // Out of position (following error)
        0x03018613, // Software limit
        0x03018614, // Limit switch
        0x00003285, // Output phase failure
        0x04018615, // Curve planning calculation error
        0x00018616, // Target position overflow
        0x05018617, // Curve planning parameters too small
    };

    for (uint32_t defaultWord : DEFAULTS) {
        if (getCode(defaultWord) == code) {
            word = defaultWord;

            return true;
        }
    }

    return false;
}

void formatErrorRegister(uint8_t errorRegister, char* buffer, size_t bufferSize) {
    // CiA 301 §1001h; bit 6 is reserved
    static const char* const BIT_NAMES[] = {"generic", "current", "voltage", "temperature", "communication", "device profile", "reserved", "manufacturer"};

    buffer[0] = '\0';

    if (errorRegister == 0) {
        snprintf(buffer, bufferSize, "none");

        return;
    }

    for (int bit = 0; bit < 8; bit++) {
        if ((errorRegister & (1 << bit)) == 0) {
            continue;
        }

        size_t length = strlen(buffer);
        snprintf(buffer + length, bufferSize - length, "%s%s", length > 0 ? ", " : "", BIT_NAMES[bit]);
    }
}
} // namespace AlarmWord
