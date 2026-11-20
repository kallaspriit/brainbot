#include "Splash.hpp"

#include "BuildInfo.hpp"
#include "ui/Theme.hpp"

// White, like the panel while it starts: its white start-up flash (unavoidable, bench 2026-10-11) becomes the splash's
// background instead of a glitch. The logo keeps its paper colours. Splash-only colours, not theme tokens
static constexpr uint32_t BACKGROUND_COLOR = 0xFFFFFF;
static constexpr uint32_t TITLE_COLOR = 0x7A7E78;
static constexpr uint32_t VERSION_COLOR = 0xA4A8A2; // Lighter than the title: there for whoever looks

// Timing, ms: the logo comes in slowly and leaves quickly; the background fade from white to the dark home is longer,
// so the drop in brightness is gentle. It only blends a fill, so it is cheap
static constexpr uint32_t CONTENT_FADE_IN_MS = 800;
static constexpr uint32_t HOLD_MS = 1500;
static constexpr uint32_t CONTENT_FADE_OUT_MS = 200;
static constexpr uint32_t BACKGROUND_FADE_OUT_MS = 400;
static constexpr int32_t TITLE_LETTER_SPACE = 6;

static void setContentOpacity(void* object, int32_t opacity) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(object), static_cast<lv_opa_t>(opacity), 0);
}

static void setBackgroundOpacity(void* object, int32_t opacity) {
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(object), static_cast<lv_opa_t>(opacity), 0);
}

static void onFadedOut(lv_anim_t* animation) {
    lv_obj_delete_async(static_cast<lv_obj_t*>(animation->var));
}

/**
 * Fades the background out and deletes the splash at the end.
 *
 * @param splash The splash
 * @param delayMs Wait before the fade starts
 */
static void startBackgroundFade(lv_obj_t* splash, uint32_t delayMs) {
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, splash);
    lv_anim_set_exec_cb(&animation, setBackgroundOpacity);
    lv_anim_set_values(&animation, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&animation, delayMs);
    lv_anim_set_duration(&animation, BACKGROUND_FADE_OUT_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&animation, onFadedOut);
    lv_anim_start(&animation);
}

static void onSplashClicked(lv_event_t* event) {
    lv_obj_t* splash = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    lv_obj_t* content = lv_obj_get_child(splash, 0);

    // Skip: drop the content and the timeline, fade the background at once
    lv_anim_delete(content, nullptr);
    lv_anim_delete(splash, nullptr);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(splash, LV_OBJ_FLAG_CLICKABLE);
    startBackgroundFade(splash, 0);
}

void showSplash(bool shouldHold) {
    // Above the top bar and the overlay layer; catches taps so nothing underneath is pressed by accident
    lv_obj_t* splash = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(splash);
    lv_obj_set_size(splash, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(splash, lv_color_hex(BACKGROUND_COLOR), 0);
    lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
    lv_obj_add_flag(splash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(splash, onSplashClicked, LV_EVENT_CLICKED, nullptr);

    // Everything on the splash fades as one: title centred, the version at the bottom
    lv_obj_t* content = lv_obj_create(splash);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(content, LV_OPA_TRANSP, 0);

    lv_obj_t* group = lv_obj_create(content);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(group);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, Theme::SPACE_XL, 0);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(group);
    lv_label_set_text(title, "SPARKY");
    lv_obj_set_style_text_font(title, Theme::FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_COLOR), 0);
    lv_obj_set_style_text_letter_space(title, TITLE_LETTER_SPACE, 0);

    // Which build this is (version.txt and the commit)
    lv_obj_t* version = lv_label_create(content);
    lv_label_set_text_fmt(version, "v%s · %s", BuildInfo::getVersion(), BuildInfo::getCommit());
    lv_obj_set_style_text_font(version, Theme::FONT_BODY_SMALL, 0);
    lv_obj_set_style_text_color(version, lv_color_hex(VERSION_COLOR), 0);
    lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -Theme::SPACE_L);

    // Content in slowly, hold, out quickly
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, content);
    lv_anim_set_exec_cb(&animation, setContentOpacity);
    lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&animation, CONTENT_FADE_IN_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);

    if (shouldHold) {
        return;
    }

    // Not applied early: it would replace the fade-in (same object and callback) and show the title at once
    lv_anim_set_early_apply(&animation, false);
    lv_anim_set_values(&animation, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&animation, CONTENT_FADE_IN_MS + HOLD_MS);
    lv_anim_set_duration(&animation, CONTENT_FADE_OUT_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
    lv_anim_start(&animation);

    // Then the background, revealing the screen underneath
    startBackgroundFade(splash, CONTENT_FADE_IN_MS + HOLD_MS + CONTENT_FADE_OUT_MS);
}
