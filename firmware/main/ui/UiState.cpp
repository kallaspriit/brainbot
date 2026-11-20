#include "UiState.hpp"

#include <cstring>

#include "esp_log.h"
#include "protocol/TorqueUnits.hpp"
#include "services/MotorService.hpp"

static const char* TAG = "UiState";

void UiState::init() {
    // Motor
    lv_subject_init_int(&motor.isConnected, 0);
    lv_subject_init_int(&motor.isRunning, 0);
    lv_subject_init_int(&motor.isLostWhileRunning, 0);
    lv_subject_init_int(&motor.isBelowPackCutoff, 0);
    lv_subject_init_int(&motor.state, static_cast<int32_t>(DriveState::UNKNOWN));
    lv_subject_init_int(&motor.statusword, 0);
    lv_subject_init_int(&motor.errorCode, 0);
    lv_subject_init_int(&motor.targetVelocityRpm, 0);
    lv_subject_init_int(&motor.targetAccelerationRpmPerSecond, 0);
    lv_subject_init_int(&motor.velocityLimitRpm, 0);
    lv_subject_init_int(&motor.accelerationLimitRpmPerSecond, 0);
    lv_subject_init_int(&motor.ratedSpeedRpm, 0);
    lv_subject_init_int(&motor.maxMotorSpeedRpm, 0);
    lv_subject_init_int(&motor.motionMode, 0);
    lv_subject_init_int(&motor.isHalted, 0);
    lv_subject_init_int(&motor.faultRecordVersion, 0);
    lv_subject_init_int(&motor.sessionLogVersion, 0);
    lv_subject_init_int(&motor.parametersVersion, 0);
    lv_subject_init_int(&motor.parametersRead, 0);
    lv_subject_init_int(&motor.parametersTotal, 0);
    lv_subject_init_int(&motor.isReadingParameters, 0);
    lv_subject_init_int(&motor.unsavedChangeCount, 0);
    lv_subject_init_int(&motor.homingMethod, 0);
    lv_subject_init_int(&motor.isParameterFlashBlank, 0);
    lv_subject_init_int(&motor.isRecoveredNotRestored, 0);
    lv_subject_init_int(&motor.needsPowerCycle, 0);
    lv_subject_init_int(&motor.calibrationState, 0);
    lv_subject_init_int(&motor.calibrationStage, 0);
    lv_subject_init_int(&motor.calibrationVersion, 0);
    lv_subject_init_int(&motor.changedReactionCount, 0);
    lv_subject_init_int(&motor.isGoWithoutEffect, 0);
    lv_subject_init_int(&motor.positionMilliturns, 0);
    lv_subject_init_int(&motor.positionDecidegrees, 0);
    lv_subject_init_int(&motor.targetPositionMilliturns, 0);
    lv_subject_init_int(&motor.isPositionRelative, 0);
    lv_subject_init_int(&motor.moveByMilliturns, 0);
    lv_subject_init_int(&motor.relativeBaseMilliturns, 0);
    lv_subject_init_int(&motor.moveSpeedRpm, 0);
    lv_subject_init_int(&motor.targetTorquePerMille, 0);
    lv_subject_init_int(&motor.ratedTorqueMilliNm, 0);
    lv_subject_init_int(&motor.targetTorqueDeciMilliNm, 0);
    lv_subject_init_int(&motor.maxTorqueDeciMilliNm, 0);
    lv_subject_init_int(&motor.torqueSlopeDeciMilliNmPerSecond, 0);
    lv_subject_init_int(&motor.torqueDeciMilliNm, 0);
    lv_subject_init_int(&motor.torqueSlopePerMillePerSecond, 0);
    lv_subject_init_int(&motor.torqueSpeedLimitForwardRpm, 0);
    lv_subject_init_int(&motor.torqueSpeedLimitReverseRpm, 0);
    lv_subject_init_int(&motor.isStepTest, 0);
    lv_subject_init_int(&motor.isStepTestPosition, 0);
    lv_subject_init_int(&motor.stepTestVelocityARpm, 0);
    lv_subject_init_int(&motor.stepTestVelocityBRpm, 0);
    lv_subject_init_int(&motor.stepTestStepMilliturns, 0);
    lv_subject_init_int(&motor.stepTestDwellMs, 0);
    lv_subject_init_int(&motor.stepTestAccelerationRpmPerSecond, 0);
    lv_subject_init_int(&motor.stepTestMoveSpeedRpm, 0);
    lv_subject_init_int(&motor.unitsPerTurn, 0);
    lv_subject_init_int(&motor.velocityRpm, 0);
    lv_subject_init_int(&motor.motionSampleCount, 0);
    lv_subject_init_int(&motor.positionCounts, 0);
    lv_subject_init_int(&motor.currentCentiamps, 0);
    lv_subject_init_int(&motor.currentPerMille, 0);
    lv_subject_init_int(&motor.busVoltageDeciV, 0);
    lv_subject_init_int(&motor.temperatureC, 0);
    lv_subject_init_int(&motor.undervoltageLimitV, 0);
    lv_subject_init_int(&motor.originalUndervoltageLimitV, 0);
    lv_subject_init_int(&motor.vendorId, 0);
    lv_subject_init_int(&motor.productCode, 0);
    lv_subject_init_int(&motor.serialNumber, 0);
    lv_subject_init_string(&motor.deviceName, deviceNameBuffer, previousDeviceNameBuffer, sizeof(deviceNameBuffer), "");
    lv_subject_init_string(&motor.hardwareVersion, hardwareVersionBuffer, previousHardwareVersionBuffer, sizeof(hardwareVersionBuffer), "");
    lv_subject_init_string(&motor.softwareVersion, softwareVersionBuffer, previousSoftwareVersionBuffer, sizeof(softwareVersionBuffer), "");
    lv_subject_init_int(&motor.nodeId, 0);
    lv_subject_init_int(&motor.bitrateCode, 0);
    lv_subject_init_int(&motor.watchdogMs, 0);
    lv_subject_init_int(&motor.heartbeatMs, 0);
    lv_subject_init_int(&motor.originalHeartbeatMs, 0);
    lv_subject_init_int(&motor.encoderIncrements, 0);
    lv_subject_init_int(&motor.ratedCurrentCentiamps, 0);
    lv_subject_init_int(&motor.statorResistanceMilliohms, 0);
    lv_subject_init_int(&motor.quickStopDecelerationCounts, 0);
    lv_subject_init_int(&motor.quickStopOptionCode, 0);
    lv_subject_init_int(&motor.maxProfileVelocityCounts, 0);
    lv_subject_init_int(&motor.canErrorState, 0);
    lv_subject_init_int(&motor.busNodeId, 1);
    lv_subject_init_int(&motor.busBitrate, 1000000);
    lv_subject_init_int(&motor.searchRoundCount, 0);
    lv_subject_init_int(&motor.canTransmitErrors, 0);
    lv_subject_init_int(&motor.canReceiveErrors, 0);
    lv_subject_init_int(&motor.canReceiveDrops, 0);
    lv_subject_init_string(&motor.message, messageBuffer, previousMessageBuffer, sizeof(messageBuffer), "");
    lv_subject_init_int(&motor.messageSequence, 0);
    lv_subject_init_int(&motor.messageLevel, 0);

    // System
    lv_subject_init_int(&system.isDisplayStarted, 0);
    lv_subject_init_int(&system.isCanStarted, 0);

    lv_subject_init_int(&latency.state, 0);
    lv_subject_init_int(&latency.kind, 0);
    lv_subject_init_int(&latency.isLedOn, 0);
    lv_subject_init_int(&latency.level, 0);
    lv_subject_init_int(&latency.levelMin, 0);
    lv_subject_init_int(&latency.levelMax, 0);
    lv_subject_init_int(&latency.hasLevels, 0);
    lv_subject_init_int(&latency.darkLevel, 0);
    lv_subject_init_int(&latency.brightLevel, 0);
    lv_subject_init_int(&latency.threshold, 0);
    lv_subject_init_int(&latency.contrastPerMille, 0);
    lv_subject_init_int(&latency.sampleCount, 0);
    lv_subject_init_int(&latency.targetCount, 0);
    lv_subject_init_int(&latency.version, 0);
}

void UiState::update(const MotorStatus& motorStatus, const SystemStatus& systemStatus) {
    // Motor. Unsigned values (serial, ids) are stored bit for bit in int32 subjects; read them back with a uint32_t cast
    lv_subject_set_int(&motor.isConnected, motorStatus.isConnected);
    lv_subject_set_int(&motor.isRunning, motorStatus.isRunning);
    lv_subject_set_int(&motor.isLostWhileRunning, motorStatus.isLostWhileRunning);
    lv_subject_set_int(&motor.isBelowPackCutoff, motorStatus.isBelowPackCutoff);
    lv_subject_set_int(&motor.state, static_cast<int32_t>(motorStatus.state));
    lv_subject_set_int(&motor.statusword, motorStatus.statusword);
    lv_subject_set_int(&motor.errorCode, motorStatus.errorCode);
    lv_subject_set_int(&motor.targetVelocityRpm, motorStatus.targetVelocityRpm);
    lv_subject_set_int(&motor.targetAccelerationRpmPerSecond, motorStatus.targetAccelerationRpmPerSecond);
    lv_subject_set_int(&motor.velocityLimitRpm, motorStatus.velocityLimitRpm);
    lv_subject_set_int(&motor.accelerationLimitRpmPerSecond, motorStatus.accelerationLimitRpmPerSecond);
    lv_subject_set_int(&motor.ratedSpeedRpm, static_cast<int32_t>(motorStatus.ratedSpeedRpm));
    lv_subject_set_int(&motor.maxMotorSpeedRpm, static_cast<int32_t>(motorStatus.maxMotorSpeedRpm));
    lv_subject_set_int(&motor.motionMode, static_cast<int32_t>(motorStatus.motionMode));
    lv_subject_set_int(&motor.isHalted, motorStatus.isHalted);
    lv_subject_set_int(&motor.faultRecordVersion, static_cast<int32_t>(motorStatus.faultRecordVersion));
    lv_subject_set_int(&motor.sessionLogVersion, static_cast<int32_t>(motorStatus.sessionLogVersion));
    lv_subject_set_int(&motor.parametersVersion, static_cast<int32_t>(motorStatus.parametersVersion));
    lv_subject_set_int(&motor.parametersRead, motorStatus.parametersRead);
    lv_subject_set_int(&motor.parametersTotal, motorStatus.parametersTotal);
    lv_subject_set_int(&motor.isReadingParameters, motorStatus.isReadingParameters);
    lv_subject_set_int(&motor.unsavedChangeCount, motorStatus.unsavedChangeCount);
    lv_subject_set_int(&motor.homingMethod, motorStatus.runChecks.homingMethod);
    lv_subject_set_int(&motor.isParameterFlashBlank, motorStatus.runChecks.isParameterFlashBlank);
    lv_subject_set_int(&motor.isRecoveredNotRestored, motorStatus.isRecoveredNotRestored);
    lv_subject_set_int(&motor.needsPowerCycle, motorStatus.needsPowerCycle);
    lv_subject_set_int(&motor.calibrationState, static_cast<int32_t>(motorStatus.calibration.state));
    lv_subject_set_int(&motor.calibrationStage, motorStatus.calibration.stage);
    lv_subject_set_int(&motor.calibrationVersion, static_cast<int32_t>(motorStatus.calibrationVersion));
    lv_subject_set_int(&motor.changedReactionCount, static_cast<int32_t>(motorStatus.faultRecord.changedReactionCount));
    lv_subject_set_int(&motor.isGoWithoutEffect, motorStatus.isGoWithoutEffect);
    lv_subject_set_int(&motor.positionMilliturns, motorStatus.positionMilliturns);
    lv_subject_set_int(&motor.positionDecidegrees, motorStatus.positionDecidegrees);
    lv_subject_set_int(&motor.targetPositionMilliturns, motorStatus.targetPositionMilliturns);
    lv_subject_set_int(&motor.isPositionRelative, motorStatus.isPositionRelative);
    lv_subject_set_int(&motor.moveByMilliturns, motorStatus.moveByMilliturns);
    lv_subject_set_int(&motor.relativeBaseMilliturns, motorStatus.relativeBaseMilliturns);
    lv_subject_set_int(&motor.moveSpeedRpm, motorStatus.moveSpeedRpm);
    lv_subject_set_int(&motor.targetTorquePerMille, motorStatus.targetTorquePerMille);
    lv_subject_set_int(&motor.ratedTorqueMilliNm, motorStatus.ratedTorqueMilliNm);
    lv_subject_set_int(&motor.targetTorqueDeciMilliNm, TorqueUnits::toDeciMilliNm(motorStatus.targetTorquePerMille, motorStatus.ratedTorqueMilliNm));
    lv_subject_set_int(&motor.maxTorqueDeciMilliNm, TorqueUnits::toDeciMilliNm(MotorService::MAX_TORQUE_PER_MILLE, motorStatus.ratedTorqueMilliNm));
    lv_subject_set_int(&motor.torqueSlopeDeciMilliNmPerSecond, TorqueUnits::toDeciMilliNm(motorStatus.torqueSlopePerMillePerSecond, motorStatus.ratedTorqueMilliNm));
    lv_subject_set_int(&motor.torqueDeciMilliNm, motorStatus.torqueDeciMilliNm);
    lv_subject_set_int(&motor.torqueSlopePerMillePerSecond, motorStatus.torqueSlopePerMillePerSecond);
    lv_subject_set_int(&motor.torqueSpeedLimitForwardRpm, motorStatus.torqueSpeedLimitForwardRpm);
    lv_subject_set_int(&motor.torqueSpeedLimitReverseRpm, motorStatus.torqueSpeedLimitReverseRpm);
    lv_subject_set_int(&motor.isStepTest, motorStatus.isStepTest);
    lv_subject_set_int(&motor.isStepTestPosition, motorStatus.isStepTestPosition);
    lv_subject_set_int(&motor.stepTestVelocityARpm, motorStatus.stepTestVelocityARpm);
    lv_subject_set_int(&motor.stepTestVelocityBRpm, motorStatus.stepTestVelocityBRpm);
    lv_subject_set_int(&motor.stepTestStepMilliturns, motorStatus.stepTestStepMilliturns);
    lv_subject_set_int(&motor.stepTestDwellMs, motorStatus.stepTestDwellMs);
    lv_subject_set_int(&motor.stepTestAccelerationRpmPerSecond, motorStatus.stepTestAccelerationRpmPerSecond);
    lv_subject_set_int(&motor.stepTestMoveSpeedRpm, motorStatus.stepTestMoveSpeedRpm);
    lv_subject_set_int(&motor.unitsPerTurn, motorStatus.unitsPerTurn);
    lv_subject_set_int(&motor.velocityRpm, motorStatus.velocityRpm);
    lv_subject_set_int(&motor.motionSampleCount, static_cast<int32_t>(motorStatus.motionSampleCount));
    lv_subject_set_int(&motor.positionCounts, motorStatus.positionCounts);
    lv_subject_set_int(&motor.currentCentiamps, motorStatus.currentCentiamps);
    lv_subject_set_int(&motor.currentPerMille, motorStatus.currentPerMille);
    lv_subject_set_int(&motor.busVoltageDeciV, motorStatus.busVoltageDeciV);
    lv_subject_set_int(&motor.temperatureC, motorStatus.temperatureC);
    lv_subject_set_int(&motor.undervoltageLimitV, static_cast<int32_t>(motorStatus.undervoltageLimitV));
    lv_subject_set_int(&motor.originalUndervoltageLimitV, static_cast<int32_t>(motorStatus.originalUndervoltageLimitV));
    lv_subject_set_int(&motor.vendorId, static_cast<int32_t>(motorStatus.vendorId));
    lv_subject_set_int(&motor.productCode, static_cast<int32_t>(motorStatus.productCode));
    lv_subject_set_int(&motor.serialNumber, static_cast<int32_t>(motorStatus.serialNumber));

    copyStringIfChanged(&motor.deviceName, motorStatus.deviceName);
    copyStringIfChanged(&motor.hardwareVersion, motorStatus.hardwareVersion);
    copyStringIfChanged(&motor.softwareVersion, motorStatus.softwareVersion);
    lv_subject_set_int(&motor.nodeId, static_cast<int32_t>(motorStatus.nodeId));
    lv_subject_set_int(&motor.bitrateCode, static_cast<int32_t>(motorStatus.bitrateCode));
    lv_subject_set_int(&motor.watchdogMs, static_cast<int32_t>(motorStatus.watchdogMs));
    lv_subject_set_int(&motor.heartbeatMs, static_cast<int32_t>(motorStatus.heartbeatMs));
    lv_subject_set_int(&motor.originalHeartbeatMs, static_cast<int32_t>(motorStatus.originalHeartbeatMs));
    lv_subject_set_int(&motor.encoderIncrements, static_cast<int32_t>(motorStatus.encoderIncrements));
    lv_subject_set_int(&motor.ratedCurrentCentiamps, static_cast<int32_t>(motorStatus.ratedCurrentCentiamps));
    lv_subject_set_int(&motor.statorResistanceMilliohms, static_cast<int32_t>(motorStatus.statorResistanceMilliohms));
    lv_subject_set_int(&motor.quickStopDecelerationCounts, static_cast<int32_t>(motorStatus.quickStopDecelerationCounts));
    lv_subject_set_int(&motor.quickStopOptionCode, motorStatus.quickStopOptionCode);
    lv_subject_set_int(&motor.maxProfileVelocityCounts, static_cast<int32_t>(motorStatus.maxProfileVelocityCounts));

    lv_subject_set_int(&motor.canErrorState, static_cast<int32_t>(motorStatus.canErrorState));
    lv_subject_set_int(&motor.busNodeId, motorStatus.busNodeId);
    lv_subject_set_int(&motor.busBitrate, static_cast<int32_t>(motorStatus.busBitrate));
    lv_subject_set_int(&motor.searchRoundCount, static_cast<int32_t>(motorStatus.searchRoundCount));
    lv_subject_set_int(&motor.canTransmitErrors, static_cast<int32_t>(motorStatus.canTransmitErrors));
    lv_subject_set_int(&motor.canReceiveErrors, static_cast<int32_t>(motorStatus.canReceiveErrors));
    lv_subject_set_int(&motor.canReceiveDrops, static_cast<int32_t>(motorStatus.canReceiveDrops));

    // Text first, so observers of the sequence number see the new text
    lv_subject_copy_string(&motor.message, motorStatus.message);
    lv_subject_set_int(&motor.messageLevel, static_cast<int32_t>(motorStatus.messageLevel));
    lv_subject_set_int(&motor.messageSequence, static_cast<int32_t>(motorStatus.messageSequence));

    // System
    lv_subject_set_int(&system.isDisplayStarted, systemStatus.isDisplayStarted);
    lv_subject_set_int(&system.isCanStarted, systemStatus.isCanStarted);
}

void UiState::copyStringIfChanged(lv_subject_t* subject, const char* text) {
    if (strcmp(lv_subject_get_string(subject), text) != 0) {
        lv_subject_copy_string(subject, text);
    }
}

void UiState::updateLatency(const LatencyStatus& latencyStatus) {
    lv_subject_set_int(&latency.state, static_cast<int32_t>(latencyStatus.state));
    lv_subject_set_int(&latency.kind, static_cast<int32_t>(latencyStatus.kind));
    lv_subject_set_int(&latency.isLedOn, latencyStatus.isLedOn);
    lv_subject_set_int(&latency.level, latencyStatus.level);
    lv_subject_set_int(&latency.levelMin, latencyStatus.levelMin);
    lv_subject_set_int(&latency.levelMax, latencyStatus.levelMax);
    lv_subject_set_int(&latency.hasLevels, latencyStatus.hasLevels);
    lv_subject_set_int(&latency.darkLevel, latencyStatus.darkLevel);
    lv_subject_set_int(&latency.brightLevel, latencyStatus.brightLevel);
    lv_subject_set_int(&latency.threshold, latencyStatus.threshold);
    lv_subject_set_int(&latency.contrastPerMille, latencyStatus.contrastPerMille);
    lv_subject_set_int(&latency.sampleCount, latencyStatus.sampleCount);
    lv_subject_set_int(&latency.targetCount, latencyStatus.targetCount);
    lv_subject_set_int(&latency.version, static_cast<int32_t>(latencyStatus.version));
}

void UiState::log() {
    ESP_LOGI(TAG,
             "motor: connected %" PRId32 " running %" PRId32 " lost while running %" PRId32 " below cutoff %" PRId32 " state %" PRId32 " statusword 0x%04" PRIX32 " error 0x%04" PRIX32
             " velocity %" PRId32 " rpm position %" PRId32 " current %" PRId32 " cA (%" PRId32 " per mille) bus %" PRId32 " dV temperature %" PRId32 " C limit %" PRId32 "/%" PRId32
             " V serial %" PRIu32,
             lv_subject_get_int(&motor.isConnected),
             lv_subject_get_int(&motor.isRunning),
             lv_subject_get_int(&motor.isLostWhileRunning),
             lv_subject_get_int(&motor.isBelowPackCutoff),
             lv_subject_get_int(&motor.state),
             lv_subject_get_int(&motor.statusword),
             lv_subject_get_int(&motor.errorCode),
             lv_subject_get_int(&motor.velocityRpm),
             lv_subject_get_int(&motor.positionCounts),
             lv_subject_get_int(&motor.currentCentiamps),
             lv_subject_get_int(&motor.currentPerMille),
             lv_subject_get_int(&motor.busVoltageDeciV),
             lv_subject_get_int(&motor.temperatureC),
             lv_subject_get_int(&motor.undervoltageLimitV),
             lv_subject_get_int(&motor.originalUndervoltageLimitV),
             static_cast<uint32_t>(lv_subject_get_int(&motor.serialNumber)));
    ESP_LOGI(TAG,
             "motor: CAN state %" PRId32 " TEC %" PRId32 " REC %" PRId32 " message #%" PRId32 " \"%s\"",
             lv_subject_get_int(&motor.canErrorState),
             lv_subject_get_int(&motor.canTransmitErrors),
             lv_subject_get_int(&motor.canReceiveErrors),
             lv_subject_get_int(&motor.messageSequence),
             lv_subject_get_string(&motor.message));
    ESP_LOGI(TAG, "system: display %" PRId32 " CAN %" PRId32, lv_subject_get_int(&system.isDisplayStarted), lv_subject_get_int(&system.isCanStarted));
    ESP_LOGI(TAG,
             "latency: state %" PRId32 " kind %" PRId32 " LED %" PRId32 " level %" PRId32 " (%" PRId32 "-%" PRId32 ") levels %" PRId32 " dark %" PRId32 " bright %" PRId32 " threshold %" PRId32
             " contrast %" PRId32 " samples %" PRId32 "/%" PRId32,
             lv_subject_get_int(&latency.state),
             lv_subject_get_int(&latency.kind),
             lv_subject_get_int(&latency.isLedOn),
             lv_subject_get_int(&latency.level),
             lv_subject_get_int(&latency.levelMin),
             lv_subject_get_int(&latency.levelMax),
             lv_subject_get_int(&latency.hasLevels),
             lv_subject_get_int(&latency.darkLevel),
             lv_subject_get_int(&latency.brightLevel),
             lv_subject_get_int(&latency.threshold),
             lv_subject_get_int(&latency.contrastPerMille),
             lv_subject_get_int(&latency.sampleCount),
             lv_subject_get_int(&latency.targetCount));
}
