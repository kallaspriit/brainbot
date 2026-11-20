# Open questions

Nothing here is decided. When one is answered, record the answer in [Decisions](Decisions.md) and remove it here.

1. **C6 firmware.** Which ESP-Hosted slave firmware to flash to the C6, and how: over the C6 UART header (hold C6_IO9 low at power-on, Waveshare FAQ) or over the air through the P4 once something talks to it. Does BLE keep working alongside Wi-Fi?
2. **A stop that survives a hung P4.** The drives raise a communication fault when nobody acknowledges their heartbeat (D-023), but with two drives on the bus each one acknowledges the other. Options: a consumer heartbeat on the drives watching the P4 (does the firmware have `1016`? The console found it missing on V0.2.5.a5), the CAN watchdog `200C:1B` on newer drive firmware, or the P4's own task watchdog resetting the board, which drops the bus.
3. **Wake word.** ESP-SR WakeNet with a stock word to start, or a custom "Hey Sparky" model. Which runs on the P4 alongside the display and camera?
4. **Audio pins.** I2S and amplifier-enable GPIOs for the ES8311/ES7210 on the 7B; the Waveshare BSP may already set them up. The schematic is the source.
5. **Server side.** Language, hosting (home server or cloud), how audio and video are streamed (WebSocket, WebRTC, RTP) and which LLM API.
6. **Coding agent.** Which agent runs the self-programming loop, how it's reached from a messenger, and what it is not allowed to change.
7. **Power wiring on Sparky.** How the board is fed from the pack, and whether there's a fuse.
