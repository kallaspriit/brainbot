#include "Dialog.hpp"

#include "ui/Theme.hpp"
#include "ui/components/Button.hpp"
#include "ui/fonts/Icons.hpp"

// How much the backdrop darkens the screen behind, as for the number pad
static constexpr lv_opa_t BACKDROP_OPACITY = LV_OPA_60;

// Tallest the body gets before it scrolls, so the panel stays inside the 600 px screen with its header and padding
static constexpr int32_t BODY_MAX_HEIGHT = 420;

// Marks a dialog's backdrop through its user data, so closeDialog() can find it from anything inside
static char dialogMarker;

/**
 * Finds the backdrop, the dialog's root, from any object inside it.
 *
 * @param object Object in the dialog
 * @return The backdrop, or nullptr if the object isn't in a dialog
 */
static lv_obj_t* findBackdrop(lv_obj_t* object) {
    lv_obj_t* candidate = object;

    while (candidate != nullptr && lv_obj_get_user_data(candidate) != &dialogMarker) {
        candidate = lv_obj_get_parent(candidate);
    }

    return candidate;
}

/**
 * Closes the dialog when the dimmed area around it is tapped.
 *
 * @param event The click event on the backdrop
 */
static void onBackdropClicked(lv_event_t* event) {
    // Only taps on the backdrop itself; the panel's own clicks don't bubble here
    if (lv_event_get_target_obj(event) == lv_event_get_current_target_obj(event)) {
        lv_obj_delete_async(lv_event_get_current_target_obj(event));
    }
}

/**
 * Closes the dialog from its CLOSE button.
 *
 * @param event The click event
 */
static void onCloseClicked(lv_event_t* event) {
    closeDialog(lv_event_get_target_obj(event));
}

lv_obj_t* openDialog(lv_obj_t* overlayLayer, const DialogOptions& options) {
    // Backdrop: dims the screen and catches every touch, so nothing behind the dialog can be pressed. Its user data
    // marks it as a dialog root for closeDialog()
    lv_obj_t* backdrop = lv_obj_create(overlayLayer);
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, BACKDROP_OPACITY, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(backdrop, &dialogMarker);
    lv_obj_add_event_cb(backdrop, onBackdropClicked, LV_EVENT_CLICKED, nullptr);

    // The panel: header with title and CLOSE, then the body
    lv_obj_t* panel = lv_obj_create(backdrop);
    lv_obj_remove_style_all(panel);
    lv_obj_add_style(panel, &Theme::surfaceStyle, 0);
    lv_obj_set_style_bg_color(panel, Theme::color(Theme::SURFACE_RAISED), 0);
    lv_obj_set_size(panel, options.width, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, Theme::SPACE_M, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(panel);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title and subtitle stacked, the close X level with them at the right
    lv_obj_t* titles = lv_obj_create(header);
    lv_obj_remove_style_all(titles);
    lv_obj_set_height(titles, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(titles, 1);
    lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(titles, Theme::SPACE_XS, 0);
    lv_obj_remove_flag(titles, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(titles, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(titles);
    lv_label_set_text(titleLabel, options.title);
    lv_obj_add_style(titleLabel, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(titleLabel, Theme::FONT_TITLE, 0);

    if (options.subtitle[0] != '\0') {
        lv_obj_t* subtitleLabel = lv_label_create(titles);
        lv_obj_set_width(subtitleLabel, lv_pct(100));
        lv_label_set_long_mode(subtitleLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(subtitleLabel, options.subtitle);
        lv_obj_add_style(subtitleLabel, &Theme::mutedTextStyle, 0);
        lv_obj_set_style_text_font(subtitleLabel, Theme::FONT_BODY_SMALL, 0);
    }

    // Square, a full touch target, icon only
    ButtonOptions closeOptions;
    closeOptions.icon = Icons::CLOSE;
    closeOptions.variant = ButtonVariant::GHOST;
    closeOptions.onClick = onCloseClicked;
    lv_obj_t* closeButton = createButton(header, closeOptions);
    lv_obj_set_width(closeButton, Theme::TOUCH_TARGET_MIN);
    lv_obj_set_style_pad_hor(closeButton, 0, 0);

    // Body: as tall as its content up to a limit, then it scrolls; it stays clickable so swipes reach it
    lv_obj_t* body = lv_obj_create(panel);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(body, BODY_MAX_HEIGHT, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, Theme::SPACE_XS, 0);
    lv_obj_set_style_text_font(body, Theme::FONT_BODY_SMALL, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_add_style(body, &Theme::scrollbarStyle, LV_PART_SCROLLBAR);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(body, Theme::SCROLLBAR_WIDTH + Theme::SPACE_S, 0);

    return body;
}

lv_obj_t* addDialogActions(lv_obj_t* body) {
    lv_obj_t* actions = lv_obj_create(lv_obj_get_parent(body));
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, Theme::SPACE_S, 0);
    lv_obj_set_style_pad_top(actions, Theme::SPACE_S, 0);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    return actions;
}

void closeDialog(lv_obj_t* objectInDialog) {
    lv_obj_t* backdrop = findBackdrop(objectInDialog);

    if (backdrop != nullptr) {
        lv_obj_delete_async(backdrop);
    }
}
