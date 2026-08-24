# Contributing

[中文说明](#中文说明)

Small, focused fixes are easiest to review. Please open an issue before a large
API change so existing display integrations are not broken by surprise.

## Before opening a pull request

1. Keep MCU-specific code behind `OLED_PortOps`; the core must not own an STM32
   handle or global HAL callback.
2. Add or update a host regression test for drawing, controller, buffer, or
   adapter behavior.
3. Run:

   ```sh
   cmake -S tests -B tests/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build tests/build
   ctest --test-dir tests/build --output-on-failure
   ```

4. Build the affected SSD1306/SH1106 configurations when the Arm toolchain is
   available.
5. Use a focused commit such as `fix(oled): clip partial refresh columns`.

Hardware reports should include the MCU, module/controller, resolution, I2C
address and clock, pull-ups, supply voltage, wiring length, and a photo or exact
failure description. Controller-compatible modules are not always electrically
or geometrically identical.

## 中文说明

较小、目的明确的改动更容易检查。准备修改公开 API 时，请先开 Issue 讨论，避免意外破坏
已有工程。

绘图、控制器、缓冲区或适配层改动应补充主机回归测试，并运行上面的测试命令；条件允许时
再构建受影响的 SSD1306/SH1106 配置。硬件问题请写明 MCU、模块/控制器、分辨率、I2C
地址和速度、上拉、供电与线长，并提供照片或准确的异常表现。
