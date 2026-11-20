# Development setup

Setting up a Windows machine to build and flash the firmware, as done on 2026-10-08. Toolchain choice: D-007 in [Decisions](Decisions.md).

## What gets installed

| Tool | Version | Why |
|---|---|---|
| [VS Code](https://code.visualstudio.com/) | current | Editor |
| ESP-IDF extension (`espressif.esp-idf-extension`) | current | Build, flash, monitor, menuconfig from VS Code; launches the installer |
| C/C++ Extension Pack (Microsoft) | current | IntelliSense from the build's compile database, clang-format on save |
| ESP-IDF | **v5.5.5** | The framework and toolchains, installed by EIM |
| [Node.js](https://nodejs.org/) | LTS (v24 used) | Only for converting fonts with `lv_font_conv` ([UI design](UI%20design.md)) |
| Git | any | ESP-IDF and the component manager need it |

## ESP-IDF

1. Install VS Code, then the **ESP-IDF** extension. Accept its offer to install the **C/C++ Extension Pack**, or install it yourself. Uninstall the PlatformIO IDE extension if present.
2. Command palette → **ESP-IDF: Open ESP-IDF Installation Manager**. It downloads and starts EIM (the ESP-IDF Installation Manager).
3. In EIM choose **Custom Installation** (not Easy: that installs the latest ESP-IDF, which may be 6.x) and pick:
   - **Target:** `esp32p4`. `esp32c6` is optional; only needed to rebuild the Wi-Fi co-processor's firmware.
   - **Version:** `v5.5.5`.
   - **Features:** `core` and `ide`. Leave gdbgui, pytest, test-specific, ci and docs off.
   - **Path:** the default `C:\esp` is fine.
   - Tick **Delete temporary installation files** (saves several GB).
4. Command palette → **ESP-IDF: Select Current ESP-IDF Version** → `v5.5.5`, then **Developer: Reload Window**. Skipping this gives "Make sure you have the esptool.py installed" and "Current ESP-IDF setup is not found", because EIM's registry (`C:\Espressif\tools\eim_idf.json`) can point at a setup that doesn't exist. The choice is stored in `firmware/.vscode/settings.json`.
5. Optional: **ESP-IDF: Doctor Command** to check the setup.
6. Open the `firmware` folder (not the repository root) and build once. IntelliSense then reads `build/compile_commands.json` through the committed `firmware/.vscode/c_cpp_properties.json` (configuration "ESP-IDF", GCC mode, `gnu++23`). Without it the C/C++ extension falls back to MSVC headers and flags errors like "namespace std has no member span" in code that builds fine. New source files are known after the next build.

The result: ESP-IDF in `C:\esp\v5.5.5\esp-idf`, tools and Python in `C:\Espressif\tools`, and an activation script `C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1` (EIM also puts an "IDF PowerShell" shortcut on the desktop).

## Building from a terminal

**Use PowerShell, not Git Bash.** ESP-IDF refuses to run under MSYS/MinGW ("MSys/Mingw is not supported").

```powershell
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1
cd firmware
idf.py build
idf.py -p COM6 flash monitor
```

The first build downloads the managed components (Waveshare BSP, LVGL and their dependencies) into `firmware/managed_components/` and takes several minutes. All commands are in `CLAUDE.md` under Commands.

## Connecting the board

- **Data:** the USB-C port marked **USB TO UART** (CH343P). It shows up as "USB-Enhanced-SERIAL CH343 (COMx)" — COM6 on the first development PC. Windows 11 installed the driver itself; otherwise install WCH's CH343SER driver.
- **Power:** a PC port or hub alone can't power the board — it brown-out boot-loops. Feed 5.0–5.1 V (2 A limit) into the RS485 connector's 5V and GND pins, or plug a charger into the other USB-C port. See [Hardware](Hardware.md).
- **No COM port at all** usually means a charge-only cable, or a hub on another computer.
- **Chip revision:** check before the first flash; firmware for rev 1.3 and rev 3.x is not interchangeable. Ours is v3.2.

```powershell
python -m esptool --chip esp32p4 -p COM6 chip_id
```

- Close any serial monitor before flashing or taking a screenshot, or it fails with "could not open port … Access is denied".
- The console runs at **921600 baud** (custom console UART in `sdkconfig.defaults`); `idf.py monitor` picks that up. The chip's ROM prints its first lines at 115200, so they look garbled in the monitor.

## Screenshots

From the ESP-IDF PowerShell (for pyserial), with no monitor open:

```powershell
python tools\screenshot.py COM6
```

The firmware renders the screen and LVGL's top layer, sends them run-length encoded over the console with a CRC, and the script writes `screenshots/<timestamp>.png` (git-ignored). About 1.7 s and 140 KB per capture. Wait a few seconds after a reset or flash before asking; the command reader starts after the display.

## Fonts

The LVGL fonts are generated from TTFs and committed, so building doesn't need Node.js. To change them (sizes, glyphs, icons), edit `tools/convert-fonts.mjs` and run `node tools/convert-fonts.mjs` from the repository root. The first run downloads the Material Symbols icon font (15 MB) into `tools/.cache/` and `lv_font_conv` through npx. Check the result with the `specimen` serial command and a screenshot. Details in [UI design](UI%20design.md#type).

## Tried and dropped

PlatformIO (pioarduino fork 55.03.312-1): its ESP-IDF builder drops the `-include` flag of Waveshare's `esp_lvgl_adapter`, so the project doesn't build. See D-007. If it was installed, `C:\Users\<user>\.platformio` can be deleted.
