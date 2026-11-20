#include "TorqueChart.hpp"

#include <algorithm>
#include <cstdlib>

#include "protocol/TorqueUnits.hpp"
#include "services/MotorService.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/TrendChart.hpp"

// Legend order; the current draws on top of the target it follows. Static, so it outlives the chart
static const TrendSeries TORQUE_SERIES[] = {
    {"Target mN·m", Theme::TEXT_MUTED},
    {"Torque mN·m", Theme::ACCENT, false, true},
    {"Speed rpm", Theme::CHART_SECONDARY, true},
};

// About 10 s at the sampling rate (MotorService::SAMPLE_INTERVAL_MS)
static constexpr uint32_t POINT_COUNT = 10000 / MotorService::SAMPLE_INTERVAL_MS;

// Torque range snaps to 5 mN·m (values in 0.1 mN·m); speed to 500 rpm
static constexpr int32_t RANGE_STEP_DECI_MILLI_NM = 50;
static constexpr int32_t RANGE_STEP_RPM = 500;

/**
 * Supplies samples from the motor service as target and current magnitudes and velocity.
 *
 * @param cursor Next sample number, advanced past the samples returned
 * @param values Receives target, current and velocity, interleaved
 * @param maxSamples How many samples fit
 * @param userData The UiContext
 * @return Number of samples written
 */
static size_t getSamples(uint32_t& cursor, int32_t* values, size_t maxSamples, void* userData) {
    UiContext* context = static_cast<UiContext*>(userData);
    int32_t ratedMilliNm = lv_subject_get_int(&context->state.motor.ratedTorqueMilliNm);
    MotionSample samples[MotorService::MOTION_HISTORY_LENGTH];
    size_t count = context->motor.getMotionSamples(cursor, samples, std::min(maxSamples, MotorService::MOTION_HISTORY_LENGTH), cursor);

    // Per mille of the rated current to 0.1 mN·m with the rated torque, as on the screen
    for (size_t index = 0; index < count; index++) {
        const MotionSample& sample = samples[index];
        values[index * 3] = sample.targetTorquePerMille == SAMPLE_NONE ? 0 : TorqueUnits::toDeciMilliNm(std::abs(sample.targetTorquePerMille), ratedMilliNm);
        values[index * 3 + 1] = sample.currentPerMille == SAMPLE_NONE ? LV_CHART_POINT_NONE : TorqueUnits::toDeciMilliNm(std::abs(sample.currentPerMille), ratedMilliNm);
        values[index * 3 + 2] = sample.velocityRpm;
    }

    return count;
}

lv_obj_t* createTorqueChart(lv_obj_t* parent, UiContext& context) {
    TrendChartOptions options;
    options.series = TORQUE_SERIES;
    options.seriesCount = sizeof(TORQUE_SERIES) / sizeof(TORQUE_SERIES[0]);
    options.pointCount = POINT_COUNT;
    options.rangeStep = RANGE_STEP_DECI_MILLI_NM;
    options.decimals = 1;
    options.source = getSamples;
    options.secondaryRangeStep = RANGE_STEP_RPM;
    options.userData = &context;

    return createTrendChart(parent, options);
}
