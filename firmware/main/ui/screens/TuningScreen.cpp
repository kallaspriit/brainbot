#include "TuningScreen.hpp"

#include "ui/Theme.hpp"
#include "ui/components/TuningTab.hpp"
#include "ui/components/UnsavedBar.hpp"

void TuningScreen::create(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(content, Theme::SPACE_L, 0);
    lv_obj_set_style_pad_ver(content, Theme::SPACE_M, 0);
    lv_obj_set_style_pad_row(content, Theme::SPACE_M, 0);

    // Unsaved gain changes and SAVE / REVERT (PAR-005)
    lv_obj_t* unsavedBar = createUnsavedBar(content, context);
    setUnsavedBarFactoryResetShown(unsavedBar, false);

    createTuningTab(content, context);
}
