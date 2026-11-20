#include "SpecimenScreen.hpp"

#include <string>

#include "ui/Navigator.hpp"
#include "ui/Theme.hpp"
#include "ui/fonts/Fonts.hpp"
#include "ui/fonts/Icons.hpp"

/**
 * Adds one line of sample text in a font.
 *
 * @param parent Container to add to
 * @param font Font to show
 * @param name Font name shown in front of the sample
 * @param sample Sample text
 */
static void addSample(lv_obj_t* parent, const lv_font_t* font, const char* name, const char* sample) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text_fmt(label, "%s  %s", name, sample);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_add_style(label, &Theme::textStyle, 0);
}

void SpecimenScreen::create(lv_obj_t* content) {
    // Taller than the content area, so it scrolls; a tap anywhere goes back
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, Theme::SPACE_M, 0);
    lv_obj_set_style_pad_row(content, Theme::SPACE_XS, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(content, onClicked, LV_EVENT_CLICKED, &context.navigator);

    // Text fonts
    const char* textSample = "Velocity 60 rpm, 18.2 V, 36 °C ±0.5 µA × → Ω …";
    addSample(content, &inter_regular_16, "Inter 16", textSample);
    addSample(content, &inter_regular_20, "Inter 20", textSample);
    addSample(content, &inter_regular_24, "Inter 24", textSample);
    addSample(content, &inter_semibold_20, "Inter SemiBold 20", "Overview Velocity Position");
    addSample(content, &inter_semibold_24, "Inter SemiBold 24", "START  STOP  Reset fault");
    addSample(content, &inter_semibold_32, "Inter SemiBold 32", "Sparky");
    addSample(content, &mono_medium_24, "Mono 24", "-1234567 0x0637 18.2");
    addSample(content, &mono_medium_32, "Mono 32", "-1234567 0x0637 18.2");
    addSample(content, &mono_medium_48, "Mono 48", "-300 rpm 12.5 V");

    // Every icon, at each size, wrapped so all of them show
    const char* icons[] = {Icons::HOME,   Icons::BACK,     Icons::OVERVIEW, Icons::VELOCITY,    Icons::POSITION,      Icons::MOVE,    Icons::WARNING, Icons::FAULT,     Icons::PARAMETERS,
                           Icons::TOOLS,  Icons::SETTINGS, Icons::START,    Icons::STOP,        Icons::STOP_CIRCLE,   Icons::REFRESH, Icons::CHECK,   Icons::CLOSE,     Icons::ADD,
                           Icons::REMOVE, Icons::CAN_BUS,  Icons::VOLTAGE,  Icons::TEMPERATURE, Icons::DISCONNECTED,  Icons::CABLE,   Icons::SEARCH,  Icons::INFO,      Icons::RESET,
                           Icons::POWER,  Icons::SYNC,     Icons::CURRENT,  Icons::CANCEL,      Icons::CHEVRON_RIGHT, Icons::EXPAND,  Icons::CHIP,    Icons::MONITORING};
    std::string iconRow;

    for (const char* icon : icons) {
        iconRow += icon;
        iconRow += " ";
    }

    const lv_font_t* iconFonts[] = {Theme::FONT_ICON_SMALL, Theme::FONT_ICON, Theme::FONT_ICON_LARGE};

    for (const lv_font_t* iconFont : iconFonts) {
        lv_obj_t* label = lv_label_create(content);
        lv_label_set_text(label, iconRow.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(label, iconFont, 0);
        lv_obj_set_style_text_color(label, Theme::color(Theme::ACCENT), 0);
    }
}

void SpecimenScreen::onClicked(lv_event_t* event) {
    static_cast<Navigator*>(lv_event_get_user_data(event))->back();
}
