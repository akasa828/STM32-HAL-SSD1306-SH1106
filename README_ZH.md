<p align="center"><strong>中文</strong> · <a href="README.md">English</a></p>

# STM32 HAL SSD1306 / SH1106 OLED 驱动

这是一套可单独移植的单色 OLED 驱动，包含基础绘图、ASCII 文字、旋转、双缓冲、I2C DMA 刷新，以及 SSD1306/SH1106 控制器配置。驱动核心不依赖 STM32 HAL；仓库同时提供可直接用 VS Code 打开并按 `F5` 刷写的 STM32F103C8T6 完整示例。

## 主要内容

- 屏幕尺寸完全由 `OLED_WIDTH`、`OLED_HEIGHT` 推导，高度须为 8 的倍数。
- 支持 SSD1306/SH1106、列偏移、镜像与 0°/90°/180°/270° 旋转。
- 支持点、线、矩形、圆、位图、ASCII 文字、数字、进度条和可选波形绘制。
- 支持单双缓冲、全屏/局部刷新，以及软硬件滚动。
- 使用 `OLED_PortOps` 隔离 DMA、恢复、计时和诊断逻辑，核心不占用固定 I2C 句柄或 HAL 全局回调。

## 快速开始

1. 下载仓库，用 VS Code 打开项目根目录。
2. 安装推荐的 ST 官方 STM32 扩展，并允许 Bundle Manager 安装工具。
3. 按下表连接 OLED 和 ST-Link，选择 Debug 或 Release 后按 `F5`。
4. 选择 `OLED demo: Build, flash and debug`，停在 `main()` 后再次继续运行。

| OLED | STM32F103C8T6 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SCL | PB6 / I2C1 SCL |
| SDA | PB7 / I2C1 SDA |

示例会显示边框和文字，并让一个小方块持续移动，用来同时检查初始化、绘图、DMA 刷新和双缓冲。

## 在自己的工程中使用

复制 `Core/OLED/`，再选择 `Core/Port/` 中的 STM32 HAL 适配层，或者自行实现 `OLED_PortOps`。必须先绑定端口，再调用 `OLED_Init()`；应用在 HAL I2C 完成/错误回调中将事件转交给适配层即可。

详细接口见 [API 文档](docs/API_ZH.md)，移植步骤见 [移植文档](docs/PORTING_ZH.md)。

主要屏幕配置位于 `Core/OLED/oled.hpp`：

```c
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_CONTROLLER OLED_CONTROLLER_SSD1306
```

项目自有代码采用 [MIT License](LICENSE)；STM32 HAL 与 CMSIS 继续遵循各自目录中的许可证，详见[第三方说明](THIRD_PARTY_LICENSES.md)。
