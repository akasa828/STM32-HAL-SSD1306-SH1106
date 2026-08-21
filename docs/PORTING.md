# Porting

[中文](PORTING_ZH.md) · **English**

1. Copy `Core/OLED/` and add `oled.cpp` to the build.
2. Implement `OLED_PortOps`, or copy the STM32 HAL adapter from `Core/Port/`.
3. Configure the physical size, controller, column offset, and mirror macros.
4. Bind the port before `OLED_Init()` and forward transfer-complete/error events.
5. Verify full and partial refresh, then test recovery by forcing a bus error.

The core does not include `main.h`, `i2c.h`, a global I2C handle, or an MCU-specific
reinitialization function. RAM use is one frame buffer, or two when double buffering
is enabled: `OLED_WIDTH × ceil(OLED_HEIGHT / 8)` bytes per buffer.
