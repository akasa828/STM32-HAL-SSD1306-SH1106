# API reference

[中文](API_ZH.md) · **English**

`OLED_BindPort()` copies and validates an `OLED_PortOps` table. `write_dma` and
`tick_ms` are required; recovery, device detection, idle handling, and diagnostic
queries are optional. Call it before any display operation.

`OLED_NotifyTxComplete()` and `OLED_NotifyError()` finish an outstanding transfer.
The application or platform adapter must call them from the matching peripheral event.

The public drawing API remains in `Core/OLED/oled.hpp`. `OLED_Init()` configures the
controller, `OLED_GRAM_Refresh()` sends a frame, and `OLED_Wait_DMA()` waits with a
size-derived timeout. Drawing functions write to the selected buffer.

The port status values are zero on success and negative on error, so the core has no
dependency on `HAL_StatusTypeDef`.
