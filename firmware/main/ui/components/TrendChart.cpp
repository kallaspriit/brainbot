#include "TrendChart.hpp"

#include <algorithm>
#include <cinttypes>

#include "ui/Theme.hpp"

// Samples fetched per update; the chart asks again on the next update for anything left
static constexpr size_t FETCH_BATCH = 64;

// 5 % of the range above and below, so a line at the limit isn't drawn on the edge
static constexpr int32_t RANGE_MARGIN_DIVISOR = 20;

// Lines
static constexpr int32_t LINE_WIDTH = 2;
static constexpr uint32_t HORIZONTAL_DIVISIONS = 5;

/**
 * What a chart needs after creation, owned by the panel and freed with it.
 */
struct TrendChart {
    TrendChartOptions options;                   // Copy of the creation options
    lv_obj_t* chart;                             // The LVGL chart
    lv_chart_series_t* series[MAX_TREND_SERIES]; // One per options.series, same order
    lv_obj_t* maxLabel;                          // Top of the range
    lv_obj_t* minLabel;                          // Bottom of the range
    lv_obj_t* secondaryMaxLabel;                 // Top of the second scale, nullptr without one
    lv_obj_t* secondaryMinLabel;                 // Bottom of the second scale, nullptr without one
    lv_timer_t* timer;                           // Fetches new samples; deleted with the panel
    uint32_t cursor;                             // Where the source got to
};

/**
 * Rounds a value down to a multiple of the range step.
 *
 * @param value The value
 * @param step The range step
 * @return The rounded value
 */
static int32_t floorToStep(int32_t value, int32_t step) {
    if (value >= 0) {
        return value / step * step;
    }

    return -((-value + step - 1) / step * step);
}

/**
 * Rounds a value up to a multiple of the range step.
 *
 * @param value The value
 * @param step The range step
 * @return The rounded value
 */
static int32_t ceilToStep(int32_t value, int32_t step) {
    return -floorToStep(-value, step);
}

/**
 * Shows a fixed-point value on a range label, without trailing zeros in the decimals ("1.5", "2", "-0.25").
 *
 * @param label The label
 * @param value The value
 * @param decimals Number of decimals in the value
 */
static void setRangeLabel(lv_obj_t* label, int32_t value, uint8_t decimals) {
    int32_t scale = 1;

    for (uint8_t digit = 0; digit < decimals; digit++) {
        scale *= 10;
    }

    int32_t magnitude = value < 0 ? -value : value;
    int32_t whole = magnitude / scale;
    int32_t fraction = magnitude % scale;
    uint8_t fractionDigits = decimals;

    while (fractionDigits > 0 && fraction % 10 == 0) {
        fraction /= 10;
        fractionDigits--;
    }

    const char* sign = value < 0 ? "-" : "";

    if (fractionDigits == 0) {
        lv_label_set_text_fmt(label, "%s%" PRId32, sign, whole);
    } else {
        lv_label_set_text_fmt(label, "%s%" PRId32 ".%0*" PRId32, sign, whole, static_cast<int>(fractionDigits), fraction);
    }
}

/**
 * Finds the range of one scale's values on the chart, snapped to its step.
 *
 * @param trendChart The chart
 * @param isSecondary Which scale
 * @param step The range step
 * @param isZeroIncluded Whether the range must reach zero
 * @param minValue Receives the bottom
 * @param maxValue Receives the top
 */
static void findRange(TrendChart* trendChart, bool isSecondary, int32_t step, bool isZeroIncluded, int32_t& minValue, int32_t& maxValue) {
    bool hasValues = isZeroIncluded;
    minValue = 0;
    maxValue = 0;

    for (size_t seriesIndex = 0; seriesIndex < trendChart->options.seriesCount; seriesIndex++) {
        if (trendChart->options.series[seriesIndex].isSecondaryAxis != isSecondary) {
            continue;
        }

        const int32_t* values = lv_chart_get_series_y_array(trendChart->chart, trendChart->series[seriesIndex]);

        for (uint32_t index = 0; index < trendChart->options.pointCount; index++) {
            if (values[index] == LV_CHART_POINT_NONE) {
                continue;
            }

            if (!hasValues) {
                minValue = values[index];
                maxValue = values[index];
                hasValues = true;
            }

            minValue = std::min(minValue, values[index]);
            maxValue = std::max(maxValue, values[index]);
        }
    }

    minValue = floorToStep(minValue, step);
    maxValue = ceilToStep(maxValue, step);

    // Flat: show a band of one step rather than a line at the edge
    if (minValue == maxValue) {
        maxValue += step;
    }
}

/**
 * Fits both scales to the values on the chart and updates their labels.
 *
 * @param trendChart The chart
 */
static void updateRange(TrendChart* trendChart) {
    const TrendChartOptions& options = trendChart->options;
    int32_t minValue = 0;
    int32_t maxValue = 0;

    findRange(trendChart, false, options.rangeStep, options.isZeroIncluded, minValue, maxValue);
    int32_t margin = (maxValue - minValue) / RANGE_MARGIN_DIVISOR;
    lv_chart_set_axis_range(trendChart->chart, LV_CHART_AXIS_PRIMARY_Y, minValue - margin, maxValue + margin);
    setRangeLabel(trendChart->maxLabel, maxValue, options.decimals);
    setRangeLabel(trendChart->minLabel, minValue, options.decimals);

    if (trendChart->secondaryMaxLabel == nullptr) {
        return;
    }

    findRange(trendChart, true, options.secondaryRangeStep, true, minValue, maxValue);
    margin = (maxValue - minValue) / RANGE_MARGIN_DIVISOR;
    lv_chart_set_axis_range(trendChart->chart, LV_CHART_AXIS_SECONDARY_Y, minValue - margin, maxValue + margin);
    setRangeLabel(trendChart->secondaryMaxLabel, maxValue, options.secondaryDecimals);
    setRangeLabel(trendChart->secondaryMinLabel, minValue, options.secondaryDecimals);
}

/**
 * Adds the samples the source has since the last update.
 *
 * @param timer The update timer; its user data is the chart state
 */
static void onUpdate(lv_timer_t* timer) {
    TrendChart* trendChart = static_cast<TrendChart*>(lv_timer_get_user_data(timer));
    const TrendChartOptions& options = trendChart->options;
    int32_t values[FETCH_BATCH * MAX_TREND_SERIES];
    size_t count = options.source(trendChart->cursor, values, FETCH_BATCH, options.userData);

    if (count == 0) {
        return;
    }

    for (size_t sample = 0; sample < count; sample++) {
        for (size_t seriesIndex = 0; seriesIndex < options.seriesCount; seriesIndex++) {
            lv_chart_set_next_value(trendChart->chart, trendChart->series[seriesIndex], values[sample * options.seriesCount + seriesIndex]);
        }
    }

    updateRange(trendChart);
}

/**
 * Frees the chart state when the panel is deleted.
 *
 * @param event The delete event; its user data is the chart state
 */
static void onDeleted(lv_event_t* event) {
    TrendChart* trendChart = static_cast<TrendChart*>(lv_event_get_user_data(event));
    lv_timer_delete(trendChart->timer);
    delete trendChart;
}

/**
 * Adds a legend entry: a coloured dash and a name.
 *
 * @param parent Legend row
 * @param color Colour token of the line
 * @param name Line name
 */
static void addLegendEntry(lv_obj_t* parent, uint32_t color, const char* name) {
    lv_obj_t* dash = lv_obj_create(parent);
    lv_obj_remove_style_all(dash);
    lv_obj_set_size(dash, Theme::SPACE_M, LINE_WIDTH + 1);
    lv_obj_set_style_bg_color(dash, Theme::color(color), 0);
    lv_obj_set_style_bg_opa(dash, LV_OPA_COVER, 0);

    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, name);
    lv_obj_add_style(label, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_pad_right(label, Theme::SPACE_S, 0);
}

/**
 * Adds one series' line to the chart, empty.
 *
 * @param trendChart The chart
 * @param seriesIndex Index in the options' series
 */
static void addSeries(TrendChart* trendChart, size_t seriesIndex) {
    const TrendSeries& series = trendChart->options.series[seriesIndex];
    lv_chart_axis_t axis = series.isSecondaryAxis ? LV_CHART_AXIS_SECONDARY_Y : LV_CHART_AXIS_PRIMARY_Y;
    trendChart->series[seriesIndex] = lv_chart_add_series(trendChart->chart, Theme::color(series.color), axis);
    lv_chart_set_all_values(trendChart->chart, trendChart->series[seriesIndex], LV_CHART_POINT_NONE);
}

lv_obj_t* createTrendChart(lv_obj_t* parent, const TrendChartOptions& options) {
    TrendChart* trendChart = new TrendChart {};
    trendChart->options = options;
    trendChart->options.seriesCount = std::min(options.seriesCount, MAX_TREND_SERIES);

    // Panel: the chart, then the legend
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_add_style(panel, &Theme::surfaceStyle, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, Theme::SPACE_S, 0);
    lv_obj_set_style_text_font(panel, Theme::FONT_BODY_SMALL, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, onDeleted, LV_EVENT_DELETE, trendChart);

    // Chart: lines only, no points, faint horizontal grid
    lv_obj_t* chart = lv_chart_create(panel);
    trendChart->chart = chart;
    lv_obj_set_width(chart, lv_pct(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_color(chart, Theme::color(Theme::BORDER), 0);
    lv_obj_set_style_line_width(chart, LINE_WIDTH, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, options.pointCount);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, HORIZONTAL_DIVISIONS, 0);

    // LVGL draws series in reverse order of adding, so the one on top is added first, then the rest in legend order
    for (size_t seriesIndex = 0; seriesIndex < trendChart->options.seriesCount; seriesIndex++) {
        if (options.series[seriesIndex].isOnTop) {
            addSeries(trendChart, seriesIndex);
        }
    }

    uint32_t secondaryColor = 0;
    bool hasSecondary = false;

    for (size_t seriesIndex = 0; seriesIndex < trendChart->options.seriesCount; seriesIndex++) {
        const TrendSeries& series = options.series[seriesIndex];

        if (!series.isOnTop) {
            addSeries(trendChart, seriesIndex);
        }

        if (series.isSecondaryAxis && !hasSecondary) {
            secondaryColor = series.color;
            hasSecondary = true;
        }
    }

    // Range labels in the chart's top and bottom left corners
    trendChart->maxLabel = lv_label_create(chart);
    lv_obj_add_style(trendChart->maxLabel, &Theme::mutedTextStyle, 0);
    lv_obj_align(trendChart->maxLabel, LV_ALIGN_TOP_LEFT, 0, 0);

    trendChart->minLabel = lv_label_create(chart);
    lv_obj_add_style(trendChart->minLabel, &Theme::mutedTextStyle, 0);
    lv_obj_align(trendChart->minLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // The second scale on the right, in its line's colour so it's clear which lines it belongs to
    if (hasSecondary) {
        trendChart->secondaryMaxLabel = lv_label_create(chart);
        lv_obj_add_style(trendChart->secondaryMaxLabel, &Theme::mutedTextStyle, 0);
        lv_obj_set_style_text_color(trendChart->secondaryMaxLabel, Theme::color(secondaryColor), 0);
        lv_obj_align(trendChart->secondaryMaxLabel, LV_ALIGN_TOP_RIGHT, 0, 0);

        trendChart->secondaryMinLabel = lv_label_create(chart);
        lv_obj_add_style(trendChart->secondaryMinLabel, &Theme::mutedTextStyle, 0);
        lv_obj_set_style_text_color(trendChart->secondaryMinLabel, Theme::color(secondaryColor), 0);
        lv_obj_align(trendChart->secondaryMinLabel, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    // Legend under the chart, in series order
    lv_obj_t* legend = lv_obj_create(panel);
    lv_obj_remove_style_all(legend);
    lv_obj_set_size(legend, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(legend, Theme::SPACE_XS, 0);
    lv_obj_remove_flag(legend, LV_OBJ_FLAG_CLICKABLE);

    for (size_t seriesIndex = 0; seriesIndex < trendChart->options.seriesCount; seriesIndex++) {
        addLegendEntry(legend, options.series[seriesIndex].color, options.series[seriesIndex].name);
    }

    // Fill with what the source still holds, then follow it; the timer goes with the panel
    updateRange(trendChart);
    trendChart->timer = lv_timer_create(onUpdate, TREND_CHART_UPDATE_INTERVAL_MS, trendChart);
    lv_timer_ready(trendChart->timer);

    return panel;
}
