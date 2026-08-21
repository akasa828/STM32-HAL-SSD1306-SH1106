# 移植说明

**中文** · [English](PORTING.md)

1. 复制 `Core/OLED/`，并把 `oled.cpp` 加入构建。
2. 实现 `OLED_PortOps`，或复制 `Core/Port/` 中的 STM32 HAL 适配层。
3. 按实际屏幕设置尺寸、控制器、列偏移和镜像宏。
4. 在 `OLED_Init()` 前绑定端口，并转发 DMA 完成与错误事件。
5. 验证全屏、局部刷新，再主动制造一次总线错误检查恢复流程。

驱动核心不包含 `main.h`、`i2c.h`、全局 I2C 句柄或特定 MCU 的重新初始化函数。每个显存缓冲占用 `OLED_WIDTH × ceil(OLED_HEIGHT / 8)` 字节，开启双缓冲时乘二。
