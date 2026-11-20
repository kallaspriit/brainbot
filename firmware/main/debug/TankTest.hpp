#pragma once

#include <cstdint>

class MotorService;
class SdoClient;

// THROWAWAY (test/gamepad branch): drive a two-motor tracked robot with the Xbox controller. While tank mode is on, the
// motor service lends out the bus and this test drives both NiMotion drives in profile velocity mode.
//
// Controls: hold LB + RB for a second (sticks centred) to arm; Y toggles fast mode (double speed); left stick Y is throttle, right stick X is turn
// (right stick alone spins in place); B disarms. A lost controller, a drive fault, pack undervoltage, a tap on the
// gamepad view or "tank stop" all quick-stop both drives and disarm.

/**
 * Keeps the bus and motor service for later; call once before the other functions.
 *
 * @param sdo The app's SDO client, used only while the motor service has lent out the bus
 * @param motor The motor service, asked to lend out the bus while tank mode is on
 */
void tankTestBegin(SdoClient& sdo, MotorService& motor);

/**
 * Turns tank mode on: takes the bus, checks both drives answer and leaves them disarmed.
 */
void tankTestOn();

/**
 * Turns tank mode off: stops both drives and hands the bus back to the motor service.
 */
void tankTestOff();

/**
 * Quick-stops both drives and disarms; safe from any task, including the LVGL task.
 */
void tankTestStop();

/**
 * Sets which node drives which track; takes effect at the next arm, refused while armed.
 *
 * @param leftNodeId Node ID of the left track's drive
 * @param rightNodeId Node ID of the right track's drive
 */
void tankTestSetNodes(uint8_t leftNodeId, uint8_t rightNodeId);

/**
 * Flips one track's direction; takes effect at once.
 *
 * @param isLeft True for the left track, false for the right
 */
void tankTestToggleInvert(bool isLeft);

/**
 * Sets the track speed at full stick; takes effect at once.
 *
 * @param maxRpm Motor speed at full stick in rpm, clamped to 10-3000
 */
void tankTestSetMaxRpm(int32_t maxRpm);

/**
 * Sets how hard the right stick turns, as a share of full speed; takes effect at once.
 *
 * @param percent Turn at full right stick in percent of the full speed, clamped to 10-100
 */
void tankTestSetTurnPercent(int32_t percent);

/**
 * Sets the profile acceleration and deceleration; takes effect at the next arm.
 *
 * @param accelerationRpmPerSecond Acceleration in rpm/s, clamped to 50-20000
 */
void tankTestSetAccelerationRpmPerSecond(int32_t accelerationRpmPerSecond);

/**
 * Logs the tank test's settings and state.
 */
void tankTestLogStatus();
