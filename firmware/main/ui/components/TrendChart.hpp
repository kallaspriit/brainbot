#pragma once

#include <cstddef>
#include <cstdint>

#include "lvgl.h"

/**
 * One line of a trend chart.
 */
struct TrendSeries {
    const char* name;             // Legend text
    uint32_t color;               // Theme colour token
    bool isSecondaryAxis = false; // On the second scale, labelled on the right in this colour
    bool isOnTop = false;         // Drawn above the other lines; otherwise earlier lines draw above later ones
};

/**
 * Supplies new samples to a trend chart.
 *
 * @param cursor Where the chart got to; the source advances it past the samples it returns (starts at 0)
 * @param values Receives the samples, interleaved: sample 0 for every series in order, then sample 1, and so on
 * @param maxSamples How many samples fit in values
 * @param userData TrendChartOptions::userData
 * @return Number of samples written
 */
using TrendSampleSource = size_t (*)(uint32_t& cursor, int32_t* values, size_t maxSamples, void* userData);

/**
 * Options for createTrendChart().
 */
struct TrendChartOptions {
    const TrendSeries* series = nullptr; // Lines in legend order; must outlive the chart
    size_t seriesCount = 0;              // Number of lines, at most MAX_TREND_SERIES
    uint32_t pointCount = 500;           // Samples visible across the width
    int32_t rangeStep = 50;              // The vertical range snaps to multiples of this
    uint8_t decimals = 0;                // Values are fixed-point with this many decimals; the range labels show them
    bool isZeroIncluded = true;          // The range always reaches zero; off to zoom in on values far from it
    int32_t secondaryRangeStep = 50;     // Second scale: snaps to multiples of this, always includes zero
    uint8_t secondaryDecimals = 0;       // Second scale: decimals of its values
    TrendSampleSource source = nullptr;  // Supplies the samples
    void* userData = nullptr;            // Passed to the source; must outlive the chart
};

// Most lines a trend chart can show
constexpr size_t MAX_TREND_SERIES = 4;

// How often a chart fetches new samples: about one 20 ms motor sample per update, near the display's 15 ms refresh
constexpr uint32_t TREND_CHART_UPDATE_INTERVAL_MS = 20;

/**
 * Creates a scrolling trend chart: a panel with a line chart and a legend under it, no title (the screen says what it is). The vertical range follows the
 * values on screen (including zero unless turned off, snapped to the range step, with a small margin) with the range in the
 * corners. Series marked isSecondaryAxis share a second scale, labelled on the right.
 *
 * The chart asks the source for new samples on its own timer (TREND_CHART_UPDATE_INTERVAL_MS), faster than the 10 Hz
 * UiState bridge, so lines move a sample at a time; it works with any service-owned sample buffer. Size is left to the caller.
 *
 * @param parent Parent object
 * @param options Series, range step and sample source
 * @return The chart's panel
 */
lv_obj_t* createTrendChart(lv_obj_t* parent, const TrendChartOptions& options);
