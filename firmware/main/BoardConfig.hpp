#pragma once

#include <cstdint>

#include "driver/gpio.h"

/**
 * Board wiring and bus settings (docs/Hardware.md, docs/NiMotion motors.md).
 */
namespace BoardConfig {
// CAN: the board's TJA1051 transceiver
constexpr gpio_num_t PIN_CAN_TX = GPIO_NUM_22;
constexpr gpio_num_t PIN_CAN_RX = GPIO_NUM_21;
constexpr uint32_t CAN_BITRATE = 1000000; // NiMotion default and what the PTU uses

// The drive under test
constexpr uint8_t DRIVE_NODE_ID = 1; // NiMotion default
constexpr int SDO_TIMEOUT_MS = 100;  // As the PTU firmware

// Latency measurement (D-034), on the P1 header: a DFR0026 light sensor (output rises with light, measured 2026-11-02)
// and an LED with a series resistor (docs/Hardware.md)
constexpr gpio_num_t PIN_LIGHT_SENSOR = GPIO_NUM_49; // ADC2 channel 0
constexpr gpio_num_t PIN_LATENCY_LED = GPIO_NUM_46;

// GPIO39-48 (IO46-48 on P1) are powered by the P4's LDO channel 4 (`VO4` on P1), so they drive nothing until it is on;
// 3300 mV passes the 3V3 rail through (ESP-IDF `ldo_ll.h` LDO_LL_RAIL_VOLTAGE_MV)
constexpr int LDO_CHANNEL_HEADER_IO = 4;
constexpr int LDO_HEADER_IO_VOLTAGE_MV = 3300;

// Pack undervoltage cutoff (D-005), on the bus voltage the drive measures: motion stops below it and is allowed again
// once the voltage has recovered past the hysteresis
constexpr int32_t PACK_CUTOFF_DECIV = 165;
constexpr int32_t PACK_CUTOFF_HYSTERESIS_DECIV = 5;
} // namespace BoardConfig
