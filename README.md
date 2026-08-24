<p align="center"><a href="README_ZH.md">中文</a> · <strong>English</strong></p>

# STM32 HAL SSD1306 / SH1106 OLED Driver

A reusable monochrome OLED driver with drawing primitives, text, rotation,
double buffering, I2C DMA refresh, and controller-specific SSD1306/SH1106
configuration. The driver core is independent of STM32 HAL; this repository
also includes a complete STM32F103C8T6 example that can be built and flashed
from VS Code with `F5`.

![Version](https://img.shields.io/badge/version-v1.0.0-blue)
[![CI](https://github.com/akasa828/STM32-HAL-SSD1306-SH1106/actions/workflows/ci.yml/badge.svg)](https://github.com/akasa828/STM32-HAL-SSD1306-SH1106/actions/workflows/ci.yml)
![STM32 HAL](https://img.shields.io/badge/STM32-HAL-03234B)
![OLED](https://img.shields.io/badge/OLED-SSD1306%20%7C%20SH1106-222222)
![License](https://img.shields.io/badge/license-MIT-green)

## What is included

- Resolution is derived from `OLED_WIDTH` and `OLED_HEIGHT`; height must be a multiple of 8.
- SSD1306 and SH1106 controller selection, column offset, mirror, and rotation support.
- Points, lines, rectangles, circles, bitmaps, ASCII text, numbers, progress bars, and optional wave drawing.
- Single or double buffering, full refresh, partial refresh, and hardware/software scrolling.
- `OLED_PortOps` decouples DMA transfer, recovery, timekeeping, and diagnostics from the display core.
- A ready-to-flash STM32F103 example using I2C1 on PB6/PB7.

## Quick start

1. Download or clone the repository and open its root directory in VS Code.
2. Install the recommended official STM32 extension and allow Bundle Manager to install its tools.
3. Connect the OLED and ST-Link, select `Debug` or `Release`, and press `F5`.
4. Choose `OLED demo: Build, flash and debug`; continue from `main()` to run the demo.

| OLED | STM32F103C8T6 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SCL | PB6 / I2C1 SCL |
| SDA | PB7 / I2C1 SDA |

The example draws a frame and text, then animates a small block so initialization,
drawing, DMA refresh, and double buffering can be checked at once.

## Reusing the driver

Copy `Core/OLED/` into your project and either use the STM32 adapter in
`Core/Port/` or implement your own `OLED_PortOps`. Bind the port before calling
`OLED_Init()`:

```c
OLED_STM32_HAL adapter = {
    .i2c = &hi2c1,
    .reinitialize = my_i2c_reinitialize,
};

OLED_STM32_HAL_Attach(&adapter);
OLED_Init();
```

Forward only the matching I2C events from the application's HAL callbacks:

```c
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *i2c)
{
    OLED_STM32_HAL_HandleTxComplete(&adapter, i2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *i2c)
{
    OLED_STM32_HAL_HandleError(&adapter, i2c);
}
```

See [API](docs/API.md) and [Porting](docs/PORTING.md) for the complete contract.

## Display configuration

```c
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_CONTROLLER OLED_CONTROLLER_SSD1306
```

The same values can be overridden by CMake, for example:

```powershell
cmake --preset Debug -DOLED_WIDTH_OVERRIDE=128 -DOLED_HEIGHT_OVERRIDE=32
cmake --build --preset Debug
```

The default I2C clock is the original project's tested configuration. Reduce it
when a module, cable length, or pull-up network is unstable.

## Used in a complete project

This driver was separated from [SD Card OVID Player](https://github.com/akasa828/SD_Card_OVID_Player),
where it drives the animated UI and double-buffered video output of an
STM32F103 SD card video player.

## Repository layout

- `Core/OLED/` — platform-neutral driver, buffers, drawing, and fonts.
- `Core/Port/` — STM32 HAL I2C adapter.
- `Core/Src/main.c` — STM32F103 demo and HAL event forwarding.
- `Drivers/` — STM32F1 HAL and CMSIS used by the example.
- `.vscode/`, `.settings/`, `cmake/` — portable VS Code, ST-Link, and build setup.

Project-owned code uses the [MIT License](LICENSE). STM32 HAL and CMSIS keep
their licenses in their respective `Drivers/` directories; see
[third-party notices](THIRD_PARTY_LICENSES.md).
