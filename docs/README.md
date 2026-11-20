# Sparky — Documentation

Development documentation. For building, flashing and driving, see the [root README](../README.md).

## Product

- [Overview](Overview.md) - What Sparky is, what exists, where it is going, known gaps
- [Glossary](Glossary.md) - CANopen, CiA 402 and motor terms

## Technical

- [Hardware](Hardware.md) - The Waveshare board, connectors, power, CAN wiring, pin map (power, wiring and enclosure parts are still the console's)
- [NiMotion motors](NiMotion%20motors.md) - Motor models, the CANopen objects and sequences we use, faults, save and restore, known traps, bench findings
- [Gamepad and tank drive](Gamepad%20and%20tank%20drive.md) - Driving with an Xbox controller over BLE: controls, tank mode, what the Bluetooth side took, gaps
- [Camera](Camera.md) - The OV5647 on MIPI-CSI with esp_video: what it took
- [Development setup](Development%20setup.md) - Installing ESP-IDF 5.5.5 on Windows, building from PowerShell, connecting the board, screenshots, problems we hit
- [Architecture](Architecture.md) - Toolchain, layers, App startup, state and command flow, UI structure, threading rules (as built for the console)
- [UI design](UI%20design.md) - Code-first LVGL approach, UX rules, colour and type tokens, fonts, component set, screenshot command

## Project

- [Milestones](Milestones.md) - High-level plan: milestones, order, status
- [Decisions](Decisions.md) - Decision log with context and status (D-001 to D-037 from the console). Check before changing direction
- [Open questions](Open%20questions.md) - Everything not settled yet

## Plans

Detailed plans per milestone go in `Plans/`, written when the milestone starts.
