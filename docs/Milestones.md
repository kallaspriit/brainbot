# Milestones

The high-level plan. Work one milestone at a time: plan it in detail with `/plan-tasks` when it starts (in `docs/Plans/`), build it, test it on the robot, then update this file with what was learned. Everything here is _proposed_ until its milestone starts.

| # | Milestone | Status |
| --- | --- | --- |
| S1 | **Drive with a gamepad.** Xbox controller over BLE, tank mixing, arm/disarm, stops on B, tap, lost controller, fault and low pack | Done 2026-11-20 |
| S2 | **Codebase cleanup.** Drop what only the console needed (parameter, calibration, recovery and latency screens, single-motor assumptions), turn the tank and gamepad tests into proper services, a robot home screen | — |
| S3 | **Wi-Fi.** Reflash the C6 to firmware that matches ESP-Hosted 3.x, connect to Wi-Fi with BLE still working, OTA updates with rollback (two app slots, self-test before marking an image valid) | — |
| S4 | **Safe autonomy base.** A stop that doesn't depend on the P4 running (heartbeat the drives watch), odometry from the encoders, commands such as "drive 1 m" and "turn 90°" with calibration | — |
| S5 | **Face and voice.** Emotions on the display, audio out to the speaker, microphones in, a wake word on the P4 (ESP-SR or a trained model) | — |
| S6 | **Server link.** Stream audio and video to a server, receive commands, report state; the server's first LLM loop | — |
| S7 | **Self-programming.** A coding agent on the server takes feature requests from a messenger, builds and ships firmware over the air | — |
