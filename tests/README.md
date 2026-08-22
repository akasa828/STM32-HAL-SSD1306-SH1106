# Host regression tests

These tests run the platform-neutral OLED core on the development computer with
a fake DMA port. They cover SSD1306, SH1106, single buffering, double buffering,
and the STM32 HAL adapter without requiring a board.

```powershell
cmake -S tests -B tests/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```
