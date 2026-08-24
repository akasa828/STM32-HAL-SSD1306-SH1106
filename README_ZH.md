<p align="center"><strong>中文</strong> · <a href="README.md">English</a></p>

<h1 align="center">STM32 HAL SSD1306 / SH1106 OLED</h1>

<p align="center">
  绘图、文字、DMA 刷新和双缓冲，同时不让显示核心绑定某一款 MCU。
</p>

<p align="center">
  <a href="https://github.com/akasa828/STM32-HAL-SSD1306-SH1106/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/akasa828/STM32-HAL-SSD1306-SH1106?sort=semver"></a>
  <a href="https://github.com/akasa828/STM32-HAL-SSD1306-SH1106/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/akasa828/STM32-HAL-SSD1306-SH1106/actions/workflows/ci.yml/badge.svg"></a>
  <img alt="STM32 HAL" src="https://img.shields.io/badge/STM32-HAL-03234B">
  <img alt="OLED" src="https://img.shields.io/badge/OLED-SSD1306%20%7C%20SH1106-222222">
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-green"></a>
</p>

<p align="center">
  <a href="#运行完整示例">运行示例</a> ·
  <a href="#移植到自己的工程">移植驱动</a> ·
  <a href="docs/API_ZH.md">API</a> ·
  <a href="docs/PORTING_ZH.md">移植说明</a> ·
  <a href="https://github.com/akasa828/STM32-HAL-SSD1306-SH1106/issues">问题反馈</a>
</p>

<p align="center">
  <img src="docs/assets/oled-stack.svg" width="900" alt="应用、绘图核心、OLED 端口与 I2C 适配层的数据流">
</p>

这个仓库处理的是“小 OLED 示例”逐渐变成应用组成部分之后的问题：屏幕通过 DMA 刷新，
程序仍能在后台缓冲绘图；总线出错时有明确的恢复边界；显示核心也不会占用一个全局
STM32 I2C 句柄。同一套驱动实际用于
[SD Card OVID Player](https://github.com/akasa828/SD_Card_OVID_Player) 的动画 UI 和视频输出。

## 为什么单独整理这个仓库

| | 这里提供的内容 |
|---|---|
| 可移植边界 | `OLED_PortOps` 提供传输、中止、恢复、计时和诊断回调，核心中不出现 HAL 类型。 |
| 不只显示文字 | 点、线、矩形、圆、位图、ASCII、数字、进度条、滚动、镜像、反显和旋转。 |
| 明确的帧处理 | 单/双缓冲、全屏/局部刷新、DMA 完成、超时和错误统计。 |
| 控制器分离 | SSD1306 与 SH1106 使用各自的初始化、列偏移和命令行为。 |
| 可直接运行 | STM32F103C8T6 工程、CubeMX 文件、CMake Presets、ST-Link 配置和 VS Code `F5` 流程。 |

## 基本支持

| 项目 | 支持范围 |
|---|---|
| 控制器 | SSD1306、SH1106 |
| 总线 | 由用户端口提供的 I2C |
| 常见尺寸 | 128×32、128×64、96×64；其他宏尺寸仍受物理控制器限制 |
| 地址 | 可配置 7 位地址，常见为 `0x3C` 或 `0x3D` |
| 显存 | 每个缓冲区占 `width × ceil(height / 8)` 字节 |
| 示例硬件 | STM32F103C8T6，PB6/PB7 上的 I2C1 DMA |

## 运行完整示例

1. 下载或克隆仓库，用 VS Code 打开**项目根目录**。
2. 安装推荐的 ST 官方 STM32 扩展，并允许 Bundle Manager 安装工具。
3. 连接 OLED 与 ST-Link。
4. 选择 `Debug` 或 `Release` 后按 `F5`，选择
   `OLED demo: Build, flash and debug`。

| OLED | STM32F103C8T6 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SCL | PB6 / I2C1 SCL |
| SDA | PB7 / I2C1 SDA |

示例会绘制边框和文字，再让一个小方块移动，可以在同一画面检查初始化、绘图、I2C DMA
和缓冲区交换。

> [!NOTE]
> 不同模块的上拉、线长、地址焊盘和控制器版本并不完全相同。显示不稳定时，应先确认地址
> 并降低 I2C 速度，而不是先修改绘图代码。

## 移植到自己的工程

先复制平台无关核心：

```text
Core/OLED/
```

STM32 HAL 工程还可以直接复制：

```text
Core/Port/oled_stm32_hal.c
Core/Port/oled_stm32_hal.h
```

初始化前绑定端口：

```c
OLED_STM32_HAL adapter = {
    .i2c = &hi2c1,
    .reinitialize = my_i2c_reinitialize,
};

OLED_STM32_HAL_Attach(&adapter);
OLED_Init();
```

只转发匹配的 HAL 事件：

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

随后绘制并送出一帧：

```c
OLED_GRAM_Clear();
OLED_Show_String("HELLO", "1206", 4, 4);
OLED_Draw_Rectang(0, 0, OLED_WIDTH - 1, OLED_HEIGHT - 1, 0);
OLED_Swap_Buffers();
```

换用其他 MCU 时，实现 `OLED_PortOps`，不使用 HAL 适配层即可。
[移植说明](docs/PORTING_ZH.md)介绍回调约定和 RAM 公式；
[API 文档](docs/API_ZH.md)说明绘图与刷新行为。

## 屏幕配置

```c
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_CONTROLLER OLED_CONTROLLER_SSD1306
#define OLED_I2C_ADDRESS_7BIT 0x3C
```

示例也可以通过 CMake 覆盖：

```powershell
cmake --preset Debug -DOLED_WIDTH_OVERRIDE=128 -DOLED_HEIGHT_OVERRIDE=32 -DOLED_I2C_ADDRESS_OVERRIDE=0x3D
cmake --build --preset Debug
```

显存计算可以适配不同宏尺寸，但实际列数、行数、寻址和命令仍由物理控制器决定。
SH1106 的硬件滚动接口会保持无操作，而不是错误发送 SSD1306 的滚动命令。

## 验证范围

每次 Push 和 Pull Request 都会检查：

| 检查 | 当前覆盖内容 |
|---|---|
| 主机回归测试 | 绘图与裁剪、控制器模式、缓冲、DMA 故障、STM32 HAL 适配层 |
| 静态分析 | `cppcheck` warning、performance 和 portability 检查 |
| 固件矩阵 | SSD1306 128×32、128×64、96×64、128×128 编译路径和 SH1106 128×64 的 Debug/Release |

其中 128×128 只验证尺寸相关代码和内存路径可以编译，并不表示某个 SSD1306 模块真的提供
128 行。电气表现和兼容控制器仍需要目标板验证。

## 项目结构

- `Core/OLED/` — 驱动核心、缓冲区、绘图、字库和 UI 辅助函数。
- `Core/Port/` — STM32 HAL I2C 适配层。
- `Core/Src/main.c` — 可以直接刷写的 STM32F103 示例。
- `tests/` — 可在电脑运行的绘图、模式和适配层回归测试。
- `docs/` — [API](docs/API_ZH.md)和[移植说明](docs/PORTING_ZH.md)。

欢迎提交模块测试结果或改进，具体见 [CONTRIBUTING.md](CONTRIBUTING.md)。如果它确实帮你
省下了一些时间，点个 Star 就够了，也会让其他嵌入式开发者更容易找到这个驱动。

项目自有代码采用 [MIT License](LICENSE)。STM32 HAL 与 CMSIS 保留各自许可证，
详见[第三方说明](THIRD_PARTY_LICENSES.md)。
