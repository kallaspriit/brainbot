#include "LatencyChart.hpp"

#include <algorithm>
#include <cinttypes>

#include "services/LatencyService.hpp"
#include "ui/Bindings.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"

static constexpr int32_t DOT_SIZE = 8;
static constexpr int32_t DOT_SIZE_DENSE = 4;    // Above DENSE_POINTS, so neighbours don't merge
static constexpr int32_t VALUE_GUTTER = 44;     // Left of the dots, for the range's top and bottom values
static constexpr int32_t EDGE_TICK_PERCENT = 6; // Ticks this close to an end align inward, so they aren't cut off
static constexpr uint32_t DENSE_POINTS = 100;
static constexpr int32_t MEDIAN_LINE_HEIGHT = 2;
static constexpr size_t MAX_BINS = 40;
static constexpr uint32_t MIN_HISTOGRAM_BINS = 5;  // Fewer and the dots say more (a self-test, a bench check)
static constexpr uint32_t SAMPLES_PER_BIN = 5;     // On average, so the bars show a shape rather than noise
static constexpr uint32_t SELF_TEST_TOP_US = 1000; // Results under this keep a 0-1 ms scale
static constexpr size_t MAX_TICKS = 6;
static constexpr size_t MAX_TICK_INTERVALS = 5;

// Axis steps to pick from, the narrowest that needs at most MAX_TICK_INTERVALS over the range
static constexpr uint32_t TICK_STEPS_US[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000};

// Bin widths to pick from, the narrowest one that keeps the spread within MAX_BINS: 50 µs is the sensor's sample
// period (a self-test), a few ms still shows a camera run's clumps a frame apart (plan LAT-007). The widest cover more
// than the longest sample (the miss timeout), so the bins never have to cut the range short
static constexpr uint32_t BIN_WIDTHS_US[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000};
static constexpr int32_t HORIZONTAL_DIVISIONS = 4;

/**
 * Parts of the chart, kept on the panel for the observer.
 */
struct LatencyChartParts {
    UiContext* context;
    lv_obj_t* guide;       // Before any run
    lv_obj_t* samplesArea; // During a run: chart and legend
    lv_obj_t* samples;     // Dots per sample
    lv_chart_series_t* series;
    lv_obj_t* medianLine;    // Over the samples chart
    lv_obj_t* samplesTop;    // Top of the range, ms
    lv_obj_t* samplesBottom; // Bottom of the range, ms
    lv_obj_t* histogramArea;
    lv_obj_t* histogram;
    lv_chart_series_t* histogramSeries;
    lv_obj_t* histogramTop;     // Highest bin's count
    lv_obj_t* histogramMedian;  // Vertical line over the histogram
    lv_obj_t* ticks[MAX_TICKS]; // Round values under the bars
    lv_obj_t* histogramRange;   // Unit and bin width
};

/**
 * The dot chart's vertical range, round values a whole number of steps apart.
 */
struct SamplesRange {
    uint32_t bottomUs;
    uint32_t topUs;
    uint32_t stepUs; // For the labels' decimals
};

/**
 * Histogram bins: the narrowest width from BIN_WIDTHS_US that keeps the spread within the bin limit.
 */
struct Bins {
    uint32_t widthUs;
    uint32_t firstUs; // Left edge of the first bin, a multiple of the width
    uint32_t count;
};

// The status with up to 200 samples; the LVGL task only, so one static copy is enough
static LatencyStatus chartStatus;

static uint8_t getDecimals(uint32_t stepUs);
static void formatTick(char* buffer, size_t bufferSize, uint32_t timeUs, uint8_t decimals);

/**
 * Picks the dot chart's range: 0-1 ms for a self-test, else round values around the samples, so a camera run's spread
 * fills the chart instead of its top quarter.
 *
 * @param status The run
 * @return The range
 */
static SamplesRange getSamplesRange(const LatencyStatus& status) {
    uint32_t lowestUs = UINT32_MAX;
    uint32_t highestUs = 0;

    for (size_t index = 0; index < status.sampleCount; index++) {
        lowestUs = std::min(lowestUs, status.samples[index].latencyUs);
        highestUs = std::max(highestUs, status.samples[index].latencyUs);
    }

    if (status.sampleCount == 0 || highestUs + highestUs / 7 < SELF_TEST_TOP_US) {
        return {0, SELF_TEST_TOP_US, SELF_TEST_TOP_US};
    }

    // At least a fifth of the highest value, so the first few samples don't zoom in on their noise
    uint32_t lowUs = std::min(lowestUs, highestUs - highestUs / 5);
    SamplesRange range = {0, 0, TICK_STEPS_US[0]};

    for (uint32_t stepUs : TICK_STEPS_US) {
        range.stepUs = stepUs;
        range.bottomUs = lowUs / stepUs * stepUs;
        range.topUs = (highestUs / stepUs + 1) * stepUs;

        if ((range.topUs - range.bottomUs) / stepUs <= MAX_TICK_INTERVALS) {
            break;
        }
    }

    return range;
}

/**
 * Shows exactly one of the three views.
 *
 * @param parts The chart's parts
 * @param shown The view to show
 */
static void showOnly(LatencyChartParts* parts, lv_obj_t* shown) {
    lv_obj_t* views[] = {parts->guide, parts->samplesArea, parts->histogramArea};

    for (lv_obj_t* view : views) {
        if (view == shown) {
            lv_obj_remove_flag(view, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void showSamples(LatencyChartParts* parts, const LatencyStatus& status) {
    uint32_t pointCount = std::max<uint32_t>(status.targetCount, 1);
    lv_chart_set_point_count(parts->samples, pointCount);

    // Values in 10 µs, so a self-test's 0.2 ms still sits on a 1 ms scale
    SamplesRange range = getSamplesRange(status);
    int32_t bottom = static_cast<int32_t>(range.bottomUs / 10);
    int32_t top = static_cast<int32_t>(range.topUs / 10);
    lv_chart_set_axis_range(parts->samples, LV_CHART_AXIS_PRIMARY_Y, bottom, top);

    char text[16];
    uint8_t decimals = getDecimals(range.stepUs);
    formatTick(text, sizeof(text), range.topUs, decimals);
    lv_label_set_text(parts->samplesTop, text);
    formatTick(text, sizeof(text), range.bottomUs, decimals);
    lv_label_set_text(parts->samplesBottom, text);

    int32_t dotSize = pointCount > DENSE_POINTS ? DOT_SIZE_DENSE : DOT_SIZE;
    lv_obj_set_style_size(parts->samples, dotSize, dotSize, LV_PART_INDICATOR);

    for (uint32_t index = 0; index < pointCount; index++) {
        int32_t value = LV_CHART_POINT_NONE;

        if (index < status.sampleCount) {
            value = static_cast<int32_t>(status.samples[index].latencyUs / 10);
        }

        lv_chart_set_value_by_id(parts->samples, parts->series, index, value);
    }

    lv_chart_refresh(parts->samples);

    // The median as a line across, placed in percent of the chart's height
    if (status.stats.count > 0) {
        int32_t median = std::max<int32_t>(static_cast<int32_t>(status.stats.medianUs / 10), bottom);
        lv_obj_set_y(parts->medianLine, lv_pct(100 - std::min<int32_t>((median - bottom) * 100 / (top - bottom), 100)));
        lv_obj_remove_flag(parts->medianLine, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(parts->medianLine, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * Picks the histogram's bins for a run's spread: no more than about one per SAMPLES_PER_BIN samples, between
 * MIN_HISTOGRAM_BINS and MAX_BINS.
 *
 * @param status The run
 * @return The bins
 */
static Bins getBins(const LatencyStatus& status) {
    const LatencyStats& stats = status.stats;
    uint32_t maxBins = std::clamp<uint32_t>(status.sampleCount / SAMPLES_PER_BIN, MIN_HISTOGRAM_BINS, MAX_BINS);
    Bins bins = {BIN_WIDTHS_US[0], 0, 1};

    for (uint32_t widthUs : BIN_WIDTHS_US) {
        bins.widthUs = widthUs;
        bins.firstUs = stats.minUs / widthUs * widthUs;
        bins.count = (stats.maxUs - bins.firstUs) / widthUs + 1;

        if (bins.count <= maxBins) {
            break;
        }
    }

    bins.count = std::min<uint32_t>(bins.count, maxBins);

    return bins;
}

/**
 * Returns how many decimals a time in ms needs to show a step exactly.
 *
 * @param stepUs The step
 * @return 0, 1 or 2
 */
static uint8_t getDecimals(uint32_t stepUs) {
    if (stepUs < 100) {
        return 2;
    }

    if (stepUs < 1000) {
        return 1;
    }

    return 0;
}

/**
 * Formats a time as ms with the given decimals, without a unit.
 *
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 * @param timeUs The time
 * @param decimals 0, 1 or 2
 */
static void formatTick(char* buffer, size_t bufferSize, uint32_t timeUs, uint8_t decimals) {
    uint32_t unitUs = decimals == 2 ? 10 : (decimals == 1 ? 100 : 1000);
    Bindings::formatFixedPoint(buffer, bufferSize, static_cast<int32_t>(timeUs / unitUs), decimals, "");
}

/**
 * Returns a position under the histogram as a percentage of its width.
 *
 * @param timeUs The time
 * @param bins The bins
 * @return 0-100
 */
static int32_t toHistogramPercent(uint32_t timeUs, const Bins& bins) {
    uint32_t spanUs = bins.count * bins.widthUs;
    uint32_t offsetUs = timeUs > bins.firstUs ? timeUs - bins.firstUs : 0;

    return static_cast<int32_t>(std::min<uint64_t>(100, static_cast<uint64_t>(offsetUs) * 100 / spanUs));
}

static void showHistogram(LatencyChartParts* parts, const LatencyStatus& status) {
    const LatencyStats& stats = status.stats;
    Bins bins = getBins(status);
    uint32_t binUs = bins.widthUs;
    uint32_t first = bins.firstUs;
    uint32_t binCount = bins.count;

    static int32_t counts[MAX_BINS];
    std::fill(counts, counts + MAX_BINS, 0);

    for (size_t index = 0; index < status.sampleCount; index++) {
        uint32_t bin = (status.samples[index].latencyUs - first) / binUs;
        counts[std::min<uint32_t>(bin, binCount - 1)]++;
    }

    int32_t highest = *std::max_element(counts, counts + binCount);
    lv_chart_set_point_count(parts->histogram, binCount);
    lv_chart_set_axis_range(parts->histogram, LV_CHART_AXIS_PRIMARY_Y, 0, std::max<int32_t>(highest, 1));

    for (uint32_t bin = 0; bin < binCount; bin++) {
        lv_chart_set_value_by_id(parts->histogram, parts->histogramSeries, bin, counts[bin]);
    }

    lv_chart_refresh(parts->histogram);
    lv_label_set_text_fmt(parts->histogramTop, "%" PRId32 " in the fullest bin", highest);

    // Round values under the bars: the narrowest step that needs at most MAX_TICK_INTERVALS
    uint32_t lastUs = first + binCount * binUs;
    uint32_t stepUs = TICK_STEPS_US[0];

    for (uint32_t candidateUs : TICK_STEPS_US) {
        stepUs = candidateUs;

        if ((lastUs - first) / stepUs <= MAX_TICK_INTERVALS) {
            break;
        }
    }

    uint8_t decimals = getDecimals(stepUs);
    uint32_t tickUs = (first + stepUs - 1) / stepUs * stepUs;

    for (lv_obj_t* tick : parts->ticks) {
        if (tickUs > lastUs) {
            lv_obj_add_flag(tick, LV_OBJ_FLAG_HIDDEN);

            continue;
        }

        char text[16];
        formatTick(text, sizeof(text), tickUs, decimals);
        lv_label_set_text(tick, text);
        // Centred on its value, but kept inside at the ends
        int32_t percent = toHistogramPercent(tickUs, bins);
        int32_t shift = percent < EDGE_TICK_PERCENT ? 0 : (percent > 100 - EDGE_TICK_PERCENT ? -100 : -50);
        lv_obj_set_x(tick, lv_pct(percent));
        lv_obj_set_style_translate_x(tick, lv_pct(shift), 0);
        lv_obj_remove_flag(tick, LV_OBJ_FLAG_HIDDEN);
        tickUs += stepUs;
    }

    // The median as a line down the bars
    lv_obj_set_x(parts->histogramMedian, lv_pct(toHistogramPercent(stats.medianUs, bins)));

    char widthText[16];
    formatTick(widthText, sizeof(widthText), binUs, getDecimals(binUs));
    lv_label_set_text_fmt(parts->histogramRange, "Latency in ms, %s ms bins    | median", widthText);
}

static void onVersionChanged(lv_observer_t* observer, lv_subject_t* subject) {
    LatencyChartParts* parts = static_cast<LatencyChartParts*>(lv_observer_get_user_data(observer));
    chartStatus = parts->context->latency.getStatus();
    bool isRun = chartStatus.state == LatencyStatus::State::LEARNING || chartStatus.state == LatencyStatus::State::MEASURING;

    // A tight result (a self-test, a bench check) fills only a bin or two, so it keeps the dots
    bool isTightResult = chartStatus.hasResult && getBins(chartStatus).count < MIN_HISTOGRAM_BINS;

    if (isRun || isTightResult) {
        showOnly(parts, parts->samplesArea);
        showSamples(parts, chartStatus);
    } else if (chartStatus.hasResult) {
        showOnly(parts, parts->histogramArea);
        showHistogram(parts, chartStatus);
    } else {
        showOnly(parts, parts->guide);
    }
}

static void onDeleted(lv_event_t* event) {
    delete static_cast<LatencyChartParts*>(lv_event_get_user_data(event));
}

/**
 * Creates a chart without its own background, with a faint horizontal grid, as the trend charts.
 *
 * @param parent Its area
 * @param type Line (dots) or bar
 * @return The chart
 */
static lv_obj_t* createChart(lv_obj_t* parent, lv_chart_type_t type) {
    lv_obj_t* chart = lv_chart_create(parent);
    lv_obj_set_width(chart, lv_pct(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_color(chart, Theme::color(Theme::BORDER), 0);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_chart_set_type(chart, type);
    lv_chart_set_div_line_count(chart, HORIZONTAL_DIVISIONS, 0);

    return chart;
}

/**
 * Creates a column area that fills the panel.
 *
 * @param parent The panel
 * @return The area
 */
static lv_obj_t* createArea(lv_obj_t* parent) {
    lv_obj_t* area = lv_obj_create(parent);
    lv_obj_remove_style_all(area);
    lv_obj_set_width(area, lv_pct(100));
    lv_obj_set_flex_grow(area, 1);
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(area, Theme::SPACE_S, 0);
    lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(area, LV_OBJ_FLAG_CLICKABLE);

    return area;
}

/**
 * Adds a muted label.
 *
 * @param parent Where
 * @param text Initial text
 * @return The label
 */
static lv_obj_t* addMutedLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_add_style(label, &Theme::mutedTextStyle, 0);

    return label;
}

lv_obj_t* createLatencyChart(lv_obj_t* parent, UiContext& context) {
    LatencyChartParts* parts = new LatencyChartParts {};
    parts->context = &context;

    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_add_style(panel, &Theme::surfaceStyle, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_text_font(panel, Theme::FONT_BODY_SMALL, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, onDeleted, LV_EVENT_DELETE, parts);

    // Before any run: how to place things
    parts->guide = createArea(panel);
    static const char* const GUIDE[] = {
        "LED at the camera's lens, or behind a diffuser: the whole picture should brighten.",
        "Sensor flat on the screen where it brightens, in its shroud.",
        "Switch the LED below: the bar above should jump. Lock the camera's exposure if it can be.",
        "SELF-TEST (sensor on the LED) once after changing the sensor or LED; then START.",
    };

    for (const char* point : GUIDE) {
        lv_obj_t* label = lv_label_create(parts->guide);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(label, "•  %s", point);
        lv_obj_add_style(label, &Theme::textStyle, 0);
        lv_obj_set_style_text_font(label, Theme::FONT_BODY, 0);
    }

    // During a run: the samples
    parts->samplesArea = createArea(panel);
    parts->samples = createChart(parts->samplesArea, LV_CHART_TYPE_LINE);
    lv_obj_set_style_line_width(parts->samples, 0, LV_PART_ITEMS);
    lv_obj_set_style_size(parts->samples, DOT_SIZE, DOT_SIZE, LV_PART_INDICATOR);
    parts->series = lv_chart_add_series(parts->samples, Theme::color(Theme::ACCENT), LV_CHART_AXIS_PRIMARY_Y);

    parts->medianLine = lv_obj_create(parts->samples);
    lv_obj_remove_style_all(parts->medianLine);
    lv_obj_set_size(parts->medianLine, lv_pct(100), MEDIAN_LINE_HEIGHT);
    lv_obj_set_style_bg_color(parts->medianLine, Theme::color(Theme::TEXT), 0);
    lv_obj_set_style_bg_opa(parts->medianLine, LV_OPA_70, 0);
    lv_obj_add_flag(parts->medianLine, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(parts->medianLine, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(parts->medianLine, LV_OBJ_FLAG_CLICKABLE);

    // The range's ends in a gutter left of the dots, so they never sit on them
    lv_obj_set_style_pad_left(parts->samples, VALUE_GUTTER, 0);
    parts->samplesTop = addMutedLabel(parts->samples, "");
    lv_obj_align(parts->samplesTop, LV_ALIGN_TOP_LEFT, -VALUE_GUTTER, 0);
    parts->samplesBottom = addMutedLabel(parts->samples, "0");
    lv_obj_align(parts->samplesBottom, LV_ALIGN_BOTTOM_LEFT, -VALUE_GUTTER, 0);

    lv_obj_t* legend = addMutedLabel(parts->samplesArea, "");
    lv_label_set_recolor(legend, true);
    lv_label_set_text_fmt(legend, "#%06" PRIX32 " •# Each sample in ms, in order    — median", Theme::ACCENT);

    // After a run: the histogram
    parts->histogramArea = createArea(panel);
    parts->histogramTop = addMutedLabel(parts->histogramArea, "");
    parts->histogram = createChart(parts->histogramArea, LV_CHART_TYPE_BAR);
    parts->histogramSeries = lv_chart_add_series(parts->histogram, Theme::color(Theme::ACCENT), LV_CHART_AXIS_PRIMARY_Y);

    parts->histogramMedian = lv_obj_create(parts->histogram);
    lv_obj_remove_style_all(parts->histogramMedian);
    lv_obj_set_size(parts->histogramMedian, MEDIAN_LINE_HEIGHT, lv_pct(100));
    lv_obj_set_style_bg_color(parts->histogramMedian, Theme::color(Theme::TEXT), 0);
    lv_obj_set_style_bg_opa(parts->histogramMedian, LV_OPA_70, 0);
    lv_obj_add_flag(parts->histogramMedian, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(parts->histogramMedian, LV_OBJ_FLAG_CLICKABLE);

    // The axis: labels centred on their value, positioned in percent of the bars' width
    lv_obj_t* axis = lv_obj_create(parts->histogramArea);
    lv_obj_remove_style_all(axis);
    lv_obj_set_size(axis, lv_pct(100), lv_font_get_line_height(Theme::FONT_BODY_SMALL));
    lv_obj_remove_flag(axis, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(axis, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(axis, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (lv_obj_t*& tick : parts->ticks) {
        tick = addMutedLabel(axis, "");
        lv_obj_set_style_text_color(tick, Theme::color(Theme::TEXT), 0);
        lv_obj_add_flag(tick, LV_OBJ_FLAG_HIDDEN);
    }

    parts->histogramRange = addMutedLabel(parts->histogramArea, "");

    showOnly(parts, parts->guide);
    lv_subject_add_observer_obj(&context.state.latency.version, onVersionChanged, panel, parts);

    return panel;
}
