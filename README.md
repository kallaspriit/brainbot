# BrainBot

LLM based self-evolving robot experiment.

Sparky is a small tracked robot that gives an LLM a body: it drives around, sees, hears and talks, and is meant to be a fun companion and assistant. The robot handles the reflexes and the low-level commands (drive 1 m, turn 90°, show a happy face); a server with an LLM makes the decisions. The goal is a robot that reprograms itself: ask for a feature in a chat, and a coding agent writes it, builds it and ships it to the robot over the air.

## Hardware

- [Waveshare ESP32-P4-WIFI6-Touch-LCD-7B](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm): ESP32-P4, 7" touch display (the face), ESP32-C6 for Wi-Fi and Bluetooth, built-in CAN, MIPI-CSI camera input, two microphones and a speaker output
- Two NiMotion BLM integrated servo motors on CAN, driving the tracks
- OV5647 camera (Raspberry Pi style module)
- Makita 18 V LXT battery pack
- 3D-printed body and tracks: SolidWorks files in [sw](sw/), STEP exports in [step](step/), Bambu Studio project in [bambu](bambu/)

See [docs/Hardware.md](docs/Hardware.md).

## Firmware

Native [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/) v5.5.5 project in [firmware](firmware/). The version is in [firmware/version.txt](firmware/version.txt); the splash shows it with the commit the build came from.

### Setup (Windows)

VS Code with the ESP-IDF and C/C++ extensions, ESP-IDF **v5.5.5** for **esp32p4**. Node.js is needed for font conversion. Full steps and the problems we hit: [docs/Development setup.md](docs/Development%20setup.md).

### Build and flash

From PowerShell (ESP-IDF does not run under Git Bash), or the ESP-IDF terminal in VS Code:

```powershell
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1
cd firmware
idf.py build
idf.py -p COMx flash monitor
```

Connect the board's **USB TO UART** port. The board ships with ESP32-P4 silicon rev 1.3 or rev 3.x, and firmware for one does not boot on the other. The default build is rev 3.x; for rev 1.3:

```powershell
idf.py -B build-rev1 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.rev1.defaults" build
```

## Driving

Tank mode starts at power-on. Press the Xbox button on the controller, then hold **LB + RB** for a second with the sticks centred to arm. Left stick drives, right stick turns, **Y** toggles fast mode, **B** stops. A tap on the screen also stops. Pairing a new controller and the serial commands: [docs/Gamepad and tank drive.md](docs/Gamepad%20and%20tank%20drive.md).

## Status

Sparky drives with an Xbox controller over Bluetooth and the camera streams to the display. Wi-Fi, the voice, the server and self-programming are next. The firmware started as a NiMotion motor debug console on the same board and still carries that console's screens. See [docs/Milestones.md](docs/Milestones.md).

## Documentation

Development documentation is in [docs](docs/README.md).

## License

[MIT](LICENSE)
