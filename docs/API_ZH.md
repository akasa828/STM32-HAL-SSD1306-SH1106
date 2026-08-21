# API 说明

**中文** · [English](API.md)

`OLED_BindPort()` 会复制并检查 `OLED_PortOps`。其中 `write_dma` 和 `tick_ms` 必须提供；恢复、设备探测、空闲钩子和诊断查询可以按需实现。任何 OLED 操作前都应先绑定。

`OLED_NotifyTxComplete()` 与 `OLED_NotifyError()` 用于结束当前 DMA 事务，应用或平台适配层应从对应外设事件中调用它们。

绘图 API 位于 `Core/OLED/oled.hpp`。`OLED_Init()` 初始化控制器，`OLED_GRAM_Refresh()` 发送一帧，`OLED_Wait_DMA()` 按显存大小推导的超时等待 DMA。所有绘图函数写入当前选中的缓冲区。

端口层成功返回 0、失败返回负数，因此核心无需依赖 `HAL_StatusTypeDef`。
