#include "ParameterEditor.hpp"

#include <algorithm>

#include "protocol/ParameterFormat.hpp"
#include "services/MotorService.hpp"
#include "ui/Navigator.hpp"
#include "ui/UiState.hpp"
#include "ui/components/NumberPad.hpp"

namespace {
/**
 * The parameter the number pad edits. One pad at a time, and it must outlive the pad, so it is file-wide.
 */
struct EditTarget {
    UiContext* context = nullptr;
    size_t position = 0;
    bool isScaled = false;                // Edited in turns, rpm or rpm/s and converted back on OK
    ParameterFormat::UserUnitScale scale; // When isScaled
    int32_t unitsPerTurn = 0;             // When isScaled
};

EditTarget editTarget;
} // namespace

/**
 * Tells whether an entry is edited in its raw form: codes shown in hex and named values have no direct number in their
 * display unit. Position units are edited in turns, rpm or rpm/s instead (see openParameterEditor).
 *
 * @param entry Table entry
 * @return True to edit the raw value
 */
static bool isEditedRaw(const Parameters::Entry& entry) {
    return ParameterFormat::isConverted(entry) && entry.decimals == 0;
}

static void onEditConfirmed(int32_t value, void* userData) {
    EditTarget* target = static_cast<EditTarget*>(userData);

    // Turns, rpm or rpm/s back to position units
    if (target->isScaled) {
        int64_t raw = ParameterFormat::fromDisplayValue(value, target->scale, target->unitsPerTurn);
        target->context->motor.requestWriteParameter(target->position, static_cast<uint32_t>(static_cast<int32_t>(raw)));

        return;
    }

    // The pad's value is in units of 10^-decimals, which is the raw value; signed types wrap as they should
    target->context->motor.requestWriteParameter(target->position, static_cast<uint32_t>(value));
}

void openParameterEditor(UiContext& context, size_t position, uint32_t currentRaw, const char* title) {
    const Parameters::Entry& entry = Parameters::ENTRIES[position];
    bool isRaw = isEditedRaw(entry);

    // The range: the manual's, or the data type's; the pad works in int32
    int64_t minimum = entry.hasRange ? entry.rangeMin : (entry.type == Parameters::Type::INT32 ? INT32_MIN : 0);
    int64_t maximum = entry.hasRange ? entry.rangeMax : INT32_MAX;

    if (!entry.hasRange) {
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

            default:
                break;
        }
    }

    editTarget.context = &context;
    editTarget.position = position;
    editTarget.unitsPerTurn = lv_subject_get_int(&context.state.motor.unitsPerTurn);
    editTarget.isScaled = ParameterFormat::getUserUnitScale(entry, editTarget.unitsPerTurn, editTarget.scale);

    NumberPadOptions options;
    options.title = title != nullptr ? title : (entry.description[0] != '\0' ? entry.description : entry.name);

    // Position units: in the unit they are shown in, with the range converted too
    if (editTarget.isScaled) {
        const ParameterFormat::UserUnitScale& scale = editTarget.scale;
        int64_t value = ParameterFormat::toValue(entry, currentRaw);
        options.unit = scale.unit;
        options.decimals = scale.decimals;
        options.value = static_cast<int32_t>(std::clamp<int64_t>(ParameterFormat::toDisplayValue(value, scale, editTarget.unitsPerTurn), INT32_MIN, INT32_MAX));
        options.min = static_cast<int32_t>(std::clamp<int64_t>(ParameterFormat::toDisplayValue(minimum, scale, editTarget.unitsPerTurn), INT32_MIN, INT32_MAX));
        options.max = static_cast<int32_t>(std::clamp<int64_t>(ParameterFormat::toDisplayValue(maximum, scale, editTarget.unitsPerTurn), INT32_MIN, INT32_MAX));
        options.onConfirm = onEditConfirmed;
        options.userData = &editTarget;
        openNumberPad(context.navigator.getOverlayLayer(), options);

        return;
    }

    options.unit = isRaw ? "(raw)" : entry.unit;
    options.value = static_cast<int32_t>(ParameterFormat::toValue(entry, currentRaw));
    options.min = static_cast<int32_t>(std::max<int64_t>(minimum, INT32_MIN));
    options.max = static_cast<int32_t>(std::min<int64_t>(maximum, INT32_MAX));
    options.decimals = isRaw ? 0 : entry.decimals;
    options.onConfirm = onEditConfirmed;
    options.userData = &editTarget;
    openNumberPad(context.navigator.getOverlayLayer(), options);
}
