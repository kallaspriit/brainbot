#include "VelocityChart.hpp"

#include <algorithm>

#include "services/MotorService.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/TrendChart.hpp"

// Legend order; actual draws on top of the target it follows. Static, so it outlives the chart
static const TrendSeries VELOCITY_SERIES[] = {
    {"Target", Theme::TEXT_MUTED},
    {"Actual", Theme::ACCENT, false, true},
    {"Current A", Theme::CHART_SECONDARY, true},
};

// About 10 s at the sampling rate (MotorService::SAMPLE_INTERVAL_MS)
static constexpr uint32_t POINT_COUNT = 10000 / MotorService::SAMPLE_INTERVAL_MS;

// Velocity range snaps to 50 rpm
static constexpr int32_t RANGE_STEP_RPM = 50;

// Current scale snaps to 0.5 A
static constexpr int32_t RANGE_STEP_CENTIAMPS = 50;

/**
 * Supplies velocity samples from the motor service as target, actual and motor current.
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
        values[index * 3] = samples[index].targetRpm;
        values[index * 3 + 1] = samples[index].velocityRpm;
        values[index * 3 + 2] = samples[index].currentCentiamps == SAMPLE_NONE ? LV_CHART_POINT_NONE : samples[index].currentCentiamps;
    }

    return count;
}

lv_obj_t* createVelocityChart(lv_obj_t* parent, UiContext& context) {
    TrendChartOptions options;
    options.series = VELOCITY_SERIES;
    options.seriesCount = sizeof(VELOCITY_SERIES) / sizeof(VELOCITY_SERIES[0]);
    options.pointCount = POINT_COUNT;
    options.rangeStep = RANGE_STEP_RPM;
    options.source = getSamples;
    options.secondaryRangeStep = RANGE_STEP_CENTIAMPS;
    options.secondaryDecimals = 2;
    options.userData = &context.motor;

    return createTrendChart(parent, options);
}
