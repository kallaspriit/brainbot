#include "LatencyScreen.hpp"

#include <cinttypes>
#include <cstdio>

#include "services/LatencyService.hpp"
#include "ui/Bindings.hpp"
#include "ui/Navigator.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/ConfirmDialog.hpp"
#include "ui/components/InfoPanel.hpp"
#include "ui/components/LatencyChart.hpp"
#include "ui/components/LevelBar.hpp"
#include "ui/components/SegmentedToggle.hpp"
#include "ui/fonts/Icons.hpp"

static constexpr int32_t ROW_NAME_WIDTH = 150;
static constexpr uint8_t LEFT_COLUMN_GROW = 3;
static constexpr uint8_t RIGHT_COLUMN_GROW = 2;

// What the self-test confirmation says (plan D7)
static const char* const SELF_TEST_POINTS[] = {
    "Press the sensor flat against the LED, so room light can't reach it.",
    "200 samples, about 10 s. A median under 1 ms passes: the console's own chain is that fast.",
    "It learns its own levels: nothing carries over to a run through the camera.",
};

// The readout's labels, kept on the column for the observers
struct LatencyReadout {
    UiContext* context;
    lv_obj_t* headlineName;
    lv_obj_t* median;
    lv_obj_t* p95;
    lv_obj_t* samples;
    lv_obj_t* range;
    lv_obj_t* spread;
    lv_obj_t* status;
    lv_obj_t* ledToggle;
    lv_obj_t* idleRow; // SELF-TEST and START
    lv_obj_t* selfTestButton;
    lv_obj_t* startButton;
    lv_obj_t* stopButton;
};

// The status with up to 200 samples; the LVGL task only
static LatencyStatus readoutStatus;

/**
 * Formats a time in µs as milliseconds, rounded: two decimals below 10 ms (a self-test's 0.19 ms), one above.
 *
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 * @param timeUs The time
 */
static void formatMs(char* buffer, size_t bufferSize, uint32_t timeUs) {
    if (timeUs < 10000) {
        uint32_t hundredths = (timeUs + 5) / 10;
        snprintf(buffer, bufferSize, "%" PRIu32 ".%02" PRIu32 " ms", hundredths / 100, hundredths % 100);

        return;
    }

    uint32_t tenths = (timeUs + 50) / 100;
    snprintf(buffer, bufferSize, "%" PRIu32 ".%" PRIu32 " ms", tenths / 10, tenths % 10);
}

static bool isRunState(int32_t state) {
    return state == static_cast<int32_t>(LatencyStatus::State::LEARNING) || state == static_cast<int32_t>(LatencyStatus::State::MEASURING);
}

static void onReadoutChanged(lv_observer_t* observer, lv_subject_t* subject) {
    LatencyReadout* readout = static_cast<LatencyReadout*>(lv_observer_get_user_data(observer));
    readoutStatus = readout->context->latency.getStatus();
    const LatencyStatus& status = readoutStatus;
    const LatencyStats& stats = status.stats;
    bool isSelfTest = status.kind == LatencyStatus::Kind::SELF_TEST;
    char text[64];
    char second[32];

    lv_label_set_text(readout->headlineName, isSelfTest ? "Self-test median" : "Median");

    // Headline: median and p95
    if (stats.count > 0) {
        formatMs(text, sizeof(text), stats.medianUs);
        lv_label_set_text(readout->median, text);
        formatMs(second, sizeof(second), stats.p95Us);
        lv_label_set_text_fmt(readout->p95, "p95 %s", second);
    } else {
        lv_label_set_text(readout->median, "-");
        lv_label_set_text(readout->p95, "p95 -");
    }

    // Rows
    if (status.targetCount > 0) {
        lv_label_set_text_fmt(readout->samples, "%u of %u, %u missed", status.sampleCount, status.targetCount, status.missCount);
    } else {
        lv_label_set_text(readout->samples, "-");
    }

    if (stats.count > 0) {
        formatMs(text, sizeof(text), stats.minUs);
        formatMs(second, sizeof(second), stats.maxUs);
        lv_label_set_text_fmt(readout->range, "%s to %s", text, second);
        formatMs(text, sizeof(text), stats.p95Us - stats.p5Us);
        lv_label_set_text(readout->spread, text);
    } else {
        lv_label_set_text(readout->range, "-");
        lv_label_set_text(readout->spread, "-");
    }

    // Status: amber or red when something is wrong, green for a passed self-test
    uint32_t color = Theme::TEXT_MUTED;

    if (status.isFailed) {
        color = Theme::DANGER;
    } else if (status.hasResult && isSelfTest) {
        color = status.isSelfTestPassed ? Theme::SUCCESS : Theme::DANGER;
    } else if (status.isRippleHigh) {
        color = Theme::WARNING;
    }

    lv_label_set_text(readout->status, status.message[0] != '\0' ? status.message : "Aim, then START");
    lv_obj_set_style_text_color(readout->status, Theme::color(color), 0);
}

static void onRunChanged(lv_observer_t* observer, lv_subject_t* subject) {
    LatencyReadout* readout = static_cast<LatencyReadout*>(lv_observer_get_user_data(observer));
    int32_t state = lv_subject_get_int(subject);
    bool isRun = isRunState(state);
    bool isSensorRunning = state != static_cast<int32_t>(LatencyStatus::State::IDLE);

    // START and SELF-TEST, or STOP, in the same place
    if (isRun) {
        lv_obj_add_flag(readout->idleRow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(readout->stopButton, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(readout->idleRow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(readout->stopButton, LV_OBJ_FLAG_HIDDEN);
    }

    // The run owns the LED; the toggle follows it then
    if (isRun || !isSensorRunning) {
        lv_obj_add_state(readout->ledToggle, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(readout->ledToggle, LV_STATE_DISABLED);
    }
}

static void onLedChanged(int32_t index, void* userData) {
    static_cast<UiContext*>(userData)->latency.setLedOn(index == 1);
}

static void onStartClicked(lv_event_t* event) {
    static_cast<UiContext*>(lv_event_get_user_data(event))->latency.requestMeasurement();
}

static void onStopClicked(lv_event_t* event) {
    static_cast<UiContext*>(lv_event_get_user_data(event))->latency.requestStop();
}

static void onSelfTestConfirmed(void* userData) {
    static_cast<LatencyService*>(userData)->requestSelfTest();
}

static void onSelfTestClicked(lv_event_t* event) {
    UiContext& context = *static_cast<UiContext*>(lv_event_get_user_data(event));

    ConfirmDialogOptions options;
    options.title = "Self-test the sensor and the LED?";
    options.subtitle = "Checks the console's own chain before blaming the camera.";
    options.points = SELF_TEST_POINTS;
    options.pointCount = sizeof(SELF_TEST_POINTS) / sizeof(SELF_TEST_POINTS[0]);
    options.writes = "Nothing is written to a motor";
    options.confirmText = "SELF-TEST";
    options.onConfirm = onSelfTestConfirmed;
    options.userData = &context.latency;
    openConfirmDialog(context.navigator.getOverlayLayer(), options);
}

static void onReadoutDeleted(lv_event_t* event) {
    delete static_cast<LatencyReadout*>(lv_event_get_user_data(event));
}

void LatencyScreen::create(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(content, Theme::SPACE_L, 0);
    lv_obj_set_style_pad_column(content, Theme::SPACE_L, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Left: the light on the sensor, then the samples or the histogram
    lv_obj_t* left = lv_obj_create(content);
    lv_obj_remove_style_all(left);
    lv_obj_set_height(left, lv_pct(100));
    lv_obj_set_flex_grow(left, LEFT_COLUMN_GROW);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, Theme::SPACE_M, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_CLICKABLE);

    createLevelBar(left, context.state.latency);
    lv_obj_t* chart = createLatencyChart(left, context);
    lv_obj_set_width(chart, lv_pct(100));
    lv_obj_set_flex_grow(chart, 1);

    // Right: the numbers and the controls
    lv_obj_t* right = lv_obj_create(content);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, RIGHT_COLUMN_GROW);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, Theme::SPACE_S, 0);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_CLICKABLE);

    createReadout(right);
}

void LatencyScreen::createReadout(lv_obj_t* parent) {
    UiState::Latency& latency = context.state.latency;
    LatencyReadout* readout = new LatencyReadout {};
    readout->context = &context;
    lv_obj_add_event_cb(parent, onReadoutDeleted, LV_EVENT_DELETE, readout);

    // Headline: the median, large, and p95 under it (plan D8)
    readout->headlineName = lv_label_create(parent);
    lv_obj_add_style(readout->headlineName, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(readout->headlineName, Theme::FONT_BODY, 0);

    readout->median = lv_label_create(parent);
    lv_obj_add_style(readout->median, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(readout->median, Theme::FONT_NUMBER_LARGE, 0);

    readout->p95 = lv_label_create(parent);
    lv_obj_add_style(readout->p95, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(readout->p95, Theme::FONT_NUMBER_SMALL, 0);

    // The rest of the statistics
    lv_obj_t* panel = createInfoPanel(parent, "Run");
    lv_obj_set_size(panel, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(panel, Theme::SPACE_S, 0);
    readout->samples = addInfoRow(panel, "Samples", ROW_NAME_WIDTH);
    readout->range = addInfoRow(panel, "Min to max", ROW_NAME_WIDTH);
    readout->spread = addInfoRow(panel, "Spread p5-p95", ROW_NAME_WIDTH);

    readout->status = lv_label_create(parent);
    lv_obj_set_width(readout->status, lv_pct(100));
    lv_label_set_long_mode(readout->status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(readout->status, Theme::FONT_BODY_SMALL, 0);
    lv_obj_set_style_pad_top(readout->status, Theme::SPACE_S, 0);

    // The controls at the bottom
    lv_obj_t* spacer = lv_obj_create(parent);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, lv_pct(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_CLICKABLE);

    SegmentedToggleOptions toggleOptions;
    toggleOptions.first = "LED off";
    toggleOptions.second = "LED on";
    toggleOptions.subject = &latency.isLedOn;
    toggleOptions.onChange = onLedChanged;
    toggleOptions.userData = &context;
    toggleOptions.isStretched = true;
    readout->ledToggle = createSegmentedToggle(parent, toggleOptions);
    lv_obj_set_width(readout->ledToggle, lv_pct(100));

    lv_obj_t* row = lv_obj_create(parent);
    readout->idleRow = row;
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    ButtonOptions selfTestOptions;
    selfTestOptions.text = "SELF-TEST";
    selfTestOptions.variant = ButtonVariant::SECONDARY;
    selfTestOptions.onClick = onSelfTestClicked;
    selfTestOptions.userData = &context;
    readout->selfTestButton = createButton(row, selfTestOptions);
    lv_obj_set_flex_grow(readout->selfTestButton, 1);

    ButtonOptions startOptions;
    startOptions.text = "START";
    startOptions.icon = Icons::START;
    startOptions.variant = ButtonVariant::PRIMARY;
    startOptions.onClick = onStartClicked;
    startOptions.userData = &context;
    readout->startButton = createButton(row, startOptions);
    lv_obj_set_flex_grow(readout->startButton, 1);

    // STOP on its own row in the same place: hidden buttons beside it would still leave their gap
    ButtonOptions stopOptions;
    stopOptions.text = "STOP";
    stopOptions.icon = Icons::STOP;
    stopOptions.variant = ButtonVariant::DANGER;
    stopOptions.onClick = onStopClicked;
    stopOptions.userData = &context;
    readout->stopButton = createButton(parent, stopOptions);
    lv_obj_set_width(readout->stopButton, lv_pct(100));

    lv_subject_add_observer_obj(&latency.version, onReadoutChanged, parent, readout);
    lv_subject_add_observer_obj(&latency.state, onRunChanged, parent, readout);
}

void LatencyScreen::onEnter() {
    context.latency.requestAim();
}

void LatencyScreen::onLeave() {
    context.latency.requestIdle();
}
