#include "ParametersScreen.hpp"

#include "ui/Theme.hpp"
#include "ui/components/ParameterList.hpp"
#include "ui/components/SetupList.hpp"
#include "ui/components/TabStrip.hpp"
#include "ui/components/UnsavedBar.hpp"

// Curated settings first, the complete list second; Tuning and Calibrate are their own screens since D-033
static const char* const TAB_LABELS[] = {"Setup", "Parameters"};
static constexpr int32_t TAB_SETUP = 0;
static constexpr int32_t TAB_PARAMETERS = 1;

// Opens on the curated settings
static constexpr int32_t FIRST_TAB = TAB_SETUP;

void ParametersScreen::create(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(content, Theme::SPACE_L, 0);
    lv_obj_set_style_pad_bottom(content, Theme::SPACE_M, 0);
    lv_obj_set_style_pad_row(content, Theme::SPACE_M, 0);

    TabStripOptions tabOptions;
    tabOptions.labels = TAB_LABELS;
    tabOptions.count = sizeof(TAB_LABELS) / sizeof(TAB_LABELS[0]);
    tabOptions.selected = FIRST_TAB;
    tabOptions.onChange = onTabChanged;
    tabOptions.userData = this;
    createTabStrip(content, tabOptions);

    // Unsaved changes and SAVE, for every tab (PAR-005); hidden while nothing is unsaved
    unsavedBar = createUnsavedBar(content, context);

    tabContent = lv_obj_create(content);
    lv_obj_remove_style_all(tabContent);
    lv_obj_set_width(tabContent, lv_pct(100));
    lv_obj_set_flex_grow(tabContent, 1);
    lv_obj_set_flex_flow(tabContent, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(tabContent, LV_OBJ_FLAG_SCROLLABLE);

    showTab(FIRST_TAB);
}

void ParametersScreen::showTab(int32_t index) {
    lv_obj_clean(tabContent);
    setUnsavedBarFactoryResetShown(unsavedBar, index == TAB_PARAMETERS);

    if (index == TAB_PARAMETERS) {
        createParameterList(tabContent, context);

        return;
    }

    createSetupList(tabContent, context);
}

void ParametersScreen::onTabChanged(int32_t index, void* userData) {
    static_cast<ParametersScreen*>(userData)->showTab(index);
}
