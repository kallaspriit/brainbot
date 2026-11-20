#include "PositionChart.hpp"

#include <algorithm>

#include "services/MotorService.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/TrendChart.hpp"

// Legend order; actual draws on top of the target it follows. Static, so it outlives the chart
static const TrendSeries POSITION_SERIES[] = {
    {"Target", Theme::TEXT_MUTED},
    {"Actual", Theme::ACCENT, false, true},
    {"Current A", Theme::CHART_SECONDARY, true},
};

// About 10 s at the sampling rate (MotorService::SAMPLE_INTERVAL_MS)
static constexpr uint32_t POINT_COUNT = 10000 / MotorService::SAMPLE_INTERVAL_MS;

// Position range snaps to half turns
static constexpr int32_t RANGE_STEP_MILLITURNS = 500;

/**
 * Turns a sample value into a chart value, with a gap where the sample has none.
 *
 * @param value Sample value or SAMPLE_NONE
 * @return Chart value or LV_CHART_POINT_NONE
 */
static int32_t toChartValue(int32_t value) {
    return value == SAMPLE_NONE ? LV_CHART_POINT_NONE : value;
}

// Current scale snaps to 0.5 A
static constexpr int32_t RANGE_STEP_CENTIAMPS = 50;

/**
 * Supplies position samples from the motor service as target, actual and motor current.
 *
 * @param cursor Next sample number, advanced past the samples returned
 * @param values Receives target, actual and current, interleaved
 * @param maxSamples How many samples fit
 * @param userData The MotorService
 * @return Number of samples written
 */
static size_t getSamples(uint32_t& cursor, int32_t* values, size_t maxSamples, void* userData) {
    MotorService* motor = static_cast<MotorService*>(userData);
    MotionSample samples[MotorService::MOTION_HISTORY_LENGTH];
    size_t count = motor->getMotionSamples(cursor, samples, std::min(maxSamples, MotorService::MOTION_HISTORY_LENGTH), cursor);

    for (size_t index = 0; index < count; index++) {
        values[index * 3] = toChartValue(samples[index].targetPositionMilliturns);
        values[index * 3 + 1] = toChartValue(samples[index].positionMilliturns);
        values[index * 3 + 2] = toChartValue(samples[index].currentCentiamps);
    }

    return count;
}

lv_obj_t* createPositionChart(lv_obj_t* parent, UiContext& context) {
    TrendChartOptions options;
    options.series = POSITION_SERIES;
    options.seriesCount = sizeof(POSITION_SERIES) / sizeof(POSITION_SERIES[0]);
    options.pointCount = POINT_COUNT;
    options.rangeStep = RANGE_STEP_MILLITURNS;
    options.decimals = 3;
    options.isZeroIncluded = false;
    options.source = getSamples;
    options.secondaryRangeStep = RANGE_STEP_CENTIAMPS;
    options.secondaryDecimals = 2;
    options.userData = &context.motor;

    return createTrendChart(parent, options);
}
