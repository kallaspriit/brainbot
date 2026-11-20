#include "BatteryIndicator.hpp"

#include <algorithm>

#include "BoardConfig.hpp"
#include "ui/Theme.hpp"
#include "ui/fonts/Icons.hpp"

// Drawn size of the battery body and its tip
static constexpr int32_t BODY_WIDTH = 34;
static constexpr int32_t BODY_HEIGHT = 18;
static constexpr int32_t TIP_WIDTH = 3;
static constexpr int32_t TIP_HEIGHT = 8;
static constexpr int32_t OUTLINE_WIDTH = 2;
static constexpr int32_t FILL_INSET = 2; // Gap between the outline and the fill

// Pack voltages for the level, 5 Li-ion cells in series (Makita 18 V LXT): about 21 V full. A rough resting-voltage
// curve, not from a datasheet; under load the reading sags and the level drops with it
static constexpr int32_t FULL_DECIV = 205;            // 4.1 V per cell
static constexpr int32_t LOW_DECIV = 175;             // 3.5 V per cell: amber below this
static constexpr int32_t EXTERNAL_SUPPLY_DECIV = 215; // Above a full pack: a bench supply
static constexpr int32_t MIN_LEVEL_PERCENT = 5;       // A sliver even when empty, so the red shows

// Children of the indicator, in creation order
static constexpr uint32_t CHILD_BODY = 0;
static constexpr uint32_t CHILD_TIP = 1;
static constexpr uint32_t CHILD_BOLT = 2;

/**
 * Shows the level for the current bus voltage.
 *
 * @param observer The observer; its target is the indicator, its user data the UiState
 * @param subject The changed subject
 */
static void onVoltageChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = static_cast<UiState*>(lv_observer_get_user_data(observer))->motor;
    lv_obj_t* indicator = lv_observer_get_target_obj(observer);
    lv_obj_t* body = lv_obj_get_child(indicator, CHILD_BODY);
    lv_obj_t* tip = lv_obj_get_child(indicator, CHILD_TIP);
    lv_obj_t* bolt = lv_obj_get_child(indicator, CHILD_BOLT);
    lv_obj_t* fill = lv_obj_get_child(body, 0);
    int32_t busVoltageDeciV = lv_subject_get_int(&motor.busVoltageDeciV);
    bool isExternal = busVoltageDeciV >= EXTERNAL_SUPPLY_DECIV;

    // No reading yet (just connected): show nothing rather than an empty red battery
    if (busVoltageDeciV <= 0) {
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(tip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bolt, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    // A bench supply: a bolt, no level
    if (isExternal) {
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(tip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(bolt, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    lv_obj_remove_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(tip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bolt, LV_OBJ_FLAG_HIDDEN);

    // Level between the cutoff (empty) and full
    int32_t span = FULL_DECIV - BoardConfig::PACK_CUTOFF_DECIV;
    int32_t level = (busVoltageDeciV - BoardConfig::PACK_CUTOFF_DECIV) * 100 / span;
    level = std::clamp<int32_t>(level, MIN_LEVEL_PERCENT, 100);
    int32_t fillWidth = (BODY_WIDTH - 2 * (OUTLINE_WIDTH + FILL_INSET)) * level / 100;
    lv_obj_set_width(fill, fillWidth);

    // Colour: red at the cutoff, amber when low, green otherwise
    uint32_t color = Theme::SUCCESS;

    if (lv_subject_get_int(&motor.isBelowPackCutoff) || busVoltageDeciV < BoardConfig::PACK_CUTOFF_DECIV) {
        color = Theme::DANGER;
    } else if (busVoltageDeciV < LOW_DECIV) {
        color = Theme::WARNING;
    }

    lv_obj_set_style_bg_color(fill, Theme::color(color), 0);
}

lv_obj_t* createBatteryIndicator(lv_obj_t* parent, UiState& state) {
    lv_obj_t* indicator = lv_obj_create(parent);
    lv_obj_remove_style_all(indicator);
    lv_obj_set_size(indicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(indicator, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);

    // Body: outline with the fill inside, left aligned
    lv_obj_t* body = lv_obj_create(indicator);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, BODY_WIDTH, BODY_HEIGHT);
    lv_obj_set_style_border_width(body, OUTLINE_WIDTH, 0);
    lv_obj_set_style_border_color(body, Theme::color(Theme::TEXT_MUTED), 0);
    lv_obj_set_style_radius(body, 4, 0);
    lv_obj_set_style_pad_all(body, FILL_INSET, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* fill = lv_obj_create(body);
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, 0, lv_pct(100));
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fill, 1, 0);

    lv_obj_t* tip = lv_obj_create(indicator);
    lv_obj_remove_style_all(tip);
    lv_obj_set_size(tip, TIP_WIDTH, TIP_HEIGHT);
    lv_obj_set_style_bg_color(tip, Theme::color(Theme::TEXT_MUTED), 0);
    lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);

    // External supply
    lv_obj_t* bolt = lv_label_create(indicator);
    lv_label_set_text(bolt, Icons::VOLTAGE);
    lv_obj_set_style_text_font(bolt, Theme::FONT_ICON_SMALL, 0);
    lv_obj_set_style_text_color(bolt, Theme::color(Theme::TEXT_MUTED), 0);
    lv_obj_add_flag(bolt, LV_OBJ_FLAG_HIDDEN);

    lv_subject_add_observer_obj(&state.motor.busVoltageDeciV, onVoltageChanged, indicator, &state);
    lv_subject_add_observer_obj(&state.motor.isBelowPackCutoff, onVoltageChanged, indicator, &state);

    return indicator;
}
