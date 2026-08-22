# Changelog

## v1.0.1 - 2026-08-23

- Linked the original OVID player as a complete usage example.
- Added executable host tests for drawing, clipping, scrolling, buffering, controller modes, DMA failures, and the STM32 HAL adapter.
- Fixed background-buffer clearing, SSD1306 partial-refresh column offsets, off-screen progress-bar wrapping, and immediate timeout accounting.
- Fixed clipped infinite lines and signed boundary handling in line, rectangle, bitmap, text, and number rendering.
- Ignored unrelated or duplicate HAL I2C callbacks and kept I2C initialized when no recovery callback is configured.
- Split transport failure reporting, framebuffer presentation, clipping, and circle scan-line drawing into focused internal functions.
- Fixed 255-pixel-wide bitmap and rectangle loops, rectangle endpoint overflow, and missing fallback diagnostic counters.

## v1.0.0 - 2026-08-21

- First independent release of the SSD1306/SH1106 OLED driver.
- Added the platform-neutral `OLED_PortOps` interface and STM32 HAL adapter.
- Added a complete STM32F103C8T6 DMA/double-buffer demo and VS Code `F5` workflow.
- Added English and Chinese documentation, build matrix, and third-party notices.
