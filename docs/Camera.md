# Camera

The camera is an unlabelled Raspberry Pi style module with an M12 "3.6mm IR 1080P" lens. It is an **OV5647**: chip ID `0x5647` read at I2C `0x36` (2026-11-10). Live video on the display works; the code is `firmware/main/debug/CameraTest.cpp`, serial command `camera` (tap or `camera stop` to close).

## What it took

- **Connection:** the board's 2-lane MIPI-CSI connector (15-pin 1.0 mm FPC). The camera shares the BSP's I2C bus (GPIO7/8, `bsp_i2c_get_handle()`). No reset or power-down pins.
- **LDO:** set `dont_init_ldo = true`; the display already holds LDO3 for the MIPI PHY.
- **Driver:** `espressif/esp_video` **2.x**. Version 1.x fails on ESP-IDF 5.5.5 (an ISP white-balance shadow register error). Sensor and ISP are enabled in `sdkconfig.defaults` (`CONFIG_CAMERA_OV5647`, the 800×640 RAW8 50 fps mode, the ISP pipeline controller for auto exposure and white balance).
- **Format:** esp_video 2 ignores the Kconfig resolution and streams the sensor's default size. Read the format with `VIDIOC_G_FMT` and change only the pixel format (RGB565).
- **Speed:** about 17–19 fps with a `memcpy` and an LVGL software blit. Faster needs the PPA or drawing straight to the panel. The camera task runs at low priority and yields each frame, or screenshots time out waiting for the display lock.
- **Image:** upright; colours fine although the lens has no IR-cut filter.

## Next

- Hardware H.264 encoding on the P4 for streaming to the server.
- Show the camera picture as one view among others rather than a full-screen test overlay.
