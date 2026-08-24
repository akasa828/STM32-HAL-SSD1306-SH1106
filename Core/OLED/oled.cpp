/**
  ******************************************************************************
  * @file    oled.c
  * @author  riochihao
  * @brief   SSD1306 4针 I2C OLED 屏幕高性能驱动
  ******************************************************************************
  */

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "stdint.h" // 引入标准平台无关整型
#include "fonts.hpp"
#include "oled.hpp"

/* 便携式 CTZ（Count Trailing Zeros）— 兼容 GCC / ARMCC / IAR */
#if defined(__GNUC__) || defined(__clang__)
  #define OLED_CTZ8(x) ((uint8_t)__builtin_ctz((unsigned)(x)))
#elif defined(__ARMCC_VERSION)
  #include <arm_compat.h>
  #define OLED_CTZ8(x) ((uint8_t)__clz(__rbit((uint32_t)(x))))
#elif defined(__ICCARM__)
  #include <intrinsics.h>
  #define OLED_CTZ8(x) ((uint8_t)__CLZ(__RBIT((uint32_t)(x))))
#else
  static inline uint8_t oled_ctz8_fallback(uint8_t val) {
      uint8_t n = 0;
      if (!val) return 8;
      while (!(val & 1u)) { val >>= 1; n++; }
      return n;
  }
  #define OLED_CTZ8(x) oled_ctz8_fallback((uint8_t)(x))
#endif



// ==================== 波形绘制（条件编译） ====================
#if OLED_ENABLE_WAVE

// 256 点正弦查找表，Q1.15 定点：sin(2π*i/256) * 32768
//用来画三角函数图像的，如果用f103c8t6做示波器可以用这个，毕竟这个芯片没有FPU
constexpr int16_t SIN_LUT[256] = {
    0,  804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512,10278,11039,11793,
    12539,13279,14010,14732,15446,16151,16846,17530,
    18204,18868,19519,20159,20787,21403,22005,22594,
    23170,23731,24279,24811,25329,25832,26319,26790,
    27245,27683,28105,28510,28898,29268,29621,29956,
    30273,30571,30852,31113,31356,31580,31785,31971,
    32137,32285,32412,32521,32609,32678,32728,32757,
    32767,32757,32728,32678,32609,32521,32412,32285,
    32137,31971,31785,31580,31356,31113,30852,30571,
    30273,29956,29621,29268,28898,28510,28105,27683,
    27245,26790,26319,25832,25329,24811,24279,23731,
    23170,22594,22005,21403,20787,20159,19519,18868,
    18204,17530,16846,16151,15446,14732,14010,13279,
    12539,11793,11039,10278, 9512, 8739, 7962, 7179,
    6393, 5602, 4808, 4011, 3212, 2410, 1608,  804,
    0, -804,-1608,-2410,-3212,-4011,-4808,-5602,
    -6393,-7179,-7962,-8739,-9512,-10278,-11039,-11793,
    -12539,-13279,-14010,-14732,-15446,-16151,-16846,-17530,
    -18204,-18868,-19519,-20159,-20787,-21403,-22005,-22594,
    -23170,-23731,-24279,-24811,-25329,-25832,-26319,-26790,
    -27245,-27683,-28105,-28510,-28898,-29268,-29621,-29956,
    -30273,-30571,-30852,-31113,-31356,-31580,-31785,-31971,
    -32137,-32285,-32412,-32521,-32609,-32678,-32728,-32757,
    -32768,-32757,-32728,-32678,-32609,-32521,-32412,-32285,
    -32137,-31971,-31785,-31580,-31356,-31113,-30852,-30571,
    -30273,-29956,-29621,-29268,-28898,-28510,-28105,-27683,
    -27245,-26790,-26319,-25832,-25329,-24811,-24279,-23731,
    -23170,-22594,-22005,-21403,-20787,-20159,-19519,-18868,
    -18204,-17530,-16846,-16151,-15446,-14732,-14010,-13279,
    -12539,-11793,-11039,-10278, -9512, -8739, -7962, -7179,
    -6393, -5602, -4808, -4011, -3212, -2410, -1608,  -804,
};

#endif /* OLED_ENABLE_WAVE */

// OLED_GRAM： [页][列] —— 每页 8 像素高，水平寻址
uint8_t OLED_GRAM[OLED_PAGES][OLED_WIDTH];

// ==================== 双缓冲支持 ====================
#if OLED_USE_DOUBLE_BUFFER
/* 后台缓冲区 — 供双缓冲绘制使用，与 OLED_GRAM 等大（1 KB） */
uint8_t OLED_BACK_BUFFER[OLED_PAGES][OLED_WIDTH];
/* 当前绘图目标指针，初始指向 OLED_GRAM（前台缓冲区） */
uint8_t (*draw_buffer)[OLED_WIDTH] = OLED_GRAM;
/* 当前选中的缓冲区 ID：0 = 前台(OLED_GRAM)，1 = 后台(OLED_BACK_BUFFER) */
uint8_t g_current_buffer_id = 0;
#endif /* OLED_USE_DOUBLE_BUFFER */

const uint8_t OLED_I2C_ADDRESS = 0x78;  // SSD1306 在 I2C 总线上的默认 7 位写地址 (0x3C << 1)
const uint8_t CMD = 0x00; //指令命令
const uint8_t DATA = 0x40; //数据命令

volatile uint8_t OLED_DMA_Busy = 0;
static volatile uint8_t s_i2c_recover_pending = 0U;
static OLED_PortOps s_port{};
static uint32_t s_port_errors = 0U;
static uint32_t s_port_timeouts = 0U;

typedef struct {
    uint16_t left;
    uint16_t right;
    uint16_t top;
    uint16_t bottom;
} OLED_ClippedRect;

#ifndef OLED_DMA_TIMEOUT_MS
/* 默认 128x64 为 114 ms，并随显存容量增长。 */
#define OLED_DMA_TIMEOUT_MS (50U + (OLED_GRAM_SIZE / 16U))
#endif

static int OLED_Clip_Physical_Rect(int16_t x, int16_t y,
                                   int16_t width, int16_t height,
                                   OLED_ClippedRect *result)
{
    if (result == NULL || width == 0 || height == 0) return 0;

    const int32_t x_end = (int32_t)x + width;
    const int32_t y_end = (int32_t)y + height;
    int32_t left = x < x_end ? x : x_end;
    int32_t right = x > x_end ? x : x_end;
    int32_t top = y < y_end ? y : y_end;
    int32_t bottom = y > y_end ? y : y_end;

    if (left >= OLED_WIDTH || right <= 0 ||
        top >= OLED_HEIGHT || bottom <= 0) {
        return 0;
    }

    if (left < 0) left = 0;
    if (right > OLED_WIDTH) right = OLED_WIDTH;
    if (top < 0) top = 0;
    if (bottom > OLED_HEIGHT) bottom = OLED_HEIGHT;
    if (left >= right || top >= bottom) return 0;

    result->left = (uint16_t)left;
    result->right = (uint16_t)right;
    result->top = (uint16_t)top;
    result->bottom = (uint16_t)bottom;
    return 1;
}

static int32_t OLED_Round_Divide(int32_t numerator, int32_t denominator)
{
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    if (numerator >= 0) {
        return (numerator + denominator / 2) / denominator;
    }
    return -((-numerator + denominator / 2) / denominator);
}

int OLED_BindPort(const OLED_PortOps *ops)
{
    if (ops == NULL || ops->write_dma == NULL || ops->tick_ms == NULL)
        return OLED_PORT_NOT_BOUND;
    if (OLED_DMA_Busy != 0U) return OLED_PORT_BUSY;
    s_port = *ops;
    OLED_DMA_Busy = 0U;
    s_i2c_recover_pending = 0U;
    s_port_errors = 0U;
    s_port_timeouts = 0U;
    return OLED_PORT_OK;
}

void OLED_NotifyTxComplete(void)
{
    if (OLED_DMA_Busy == 0U) return;
    OLED_DMA_Busy = 0U;
    if (s_port.on_success != NULL) s_port.on_success(s_port.context);
}

void OLED_NotifyError(void)
{
    if (OLED_DMA_Busy == 0U) return;
    OLED_DMA_Busy = 0U;
    s_port_errors++;
    if (s_port.on_failure != NULL) s_port.on_failure(s_port.context, 0U);
    s_i2c_recover_pending = 1U;
}

static void OLED_Recover_I2C(void);

static void OLED_Record_Transfer_Failure(int status)
{
    const uint8_t timed_out = status == OLED_PORT_TIMEOUT ? 1U : 0U;
    ++s_port_errors;
    s_port_timeouts += timed_out;
    if (s_port.on_failure != NULL) {
        s_port.on_failure(s_port.context, timed_out);
    }
}

/**
 * @brief  封装 DMA 发送，等待前次传输完成后启动新的 I2C Mem Write DMA
 * @return 1=成功启动, 0=HAL 返回错误
 */
static int OLED_DMA_Send(uint16_t mode, uint8_t* data, uint16_t size)
{
    OLED_Wait_DMA();
    if (s_port.write_dma == NULL) return 0;
    OLED_DMA_Busy = 1U;
    const int status = s_port.write_dma(s_port.context, OLED_I2C_ADDRESS,
                                        (uint8_t)mode, data, size);
    if (status != OLED_PORT_OK) {
        OLED_Record_Transfer_Failure(status);
        OLED_Recover_I2C();
        return 0;
    }
    return 1;
}

/** 真正停止已卡住的 DMA，复位并重新初始化 I2C1。 */
static void OLED_Recover_I2C(void)
{
    OLED_DMA_Busy = 0U;
    s_i2c_recover_pending = 0U;
    if (s_port.abort_dma != NULL) (void)s_port.abort_dma(s_port.context);
    if (s_port.recover != NULL) (void)s_port.recover(s_port.context);
    s_i2c_recover_pending = 0U;
}

/**
 * @brief  阻塞——等待DMA传送完成（带超时保护）
 * @note   超时按 OLED_GRAM_SIZE 给予余量。超时后中止 DMA，
 *         并复位、重新初始化 I2C，避免继续覆盖未完成的传输。
 */
void OLED_Wait_DMA()
{
    if (s_i2c_recover_pending) OLED_Recover_I2C();
    if (s_port.tick_ms == NULL) return;
    uint32_t start = s_port.tick_ms(s_port.context);
    while (OLED_DMA_Busy) {
        if ((s_port.tick_ms(s_port.context) - start) > OLED_DMA_TIMEOUT_MS) {
            OLED_Record_Transfer_Failure(OLED_PORT_TIMEOUT);
            OLED_Recover_I2C();
            break;
        }
        if (s_port.idle != NULL) s_port.idle(s_port.context);
    }
    if (s_i2c_recover_pending) OLED_Recover_I2C();
}

uint32_t OLED_Get_I2C_Error_Count(void) {
    return s_port.get_error_count != NULL
        ? s_port.get_error_count(s_port.context) : s_port_errors;
}
uint32_t OLED_Get_I2C_Timeout_Count(void) {
    return s_port.get_timeout_count != NULL
        ? s_port.get_timeout_count(s_port.context) : s_port_timeouts;
}
uint32_t OLED_Get_I2C_Clock(void) {
    return s_port.get_clock_hz != NULL ? s_port.get_clock_hz(s_port.context) : 0U;
}

/**
 * @brief  向 OLED 写入一个命令/数据字节 (DMA)
 * @note   dma_buf 必须为 static：DMA 异步读取该地址，函数返回时 DMA 可能仍在进行。
 *         若改为局部变量，栈帧释放后 DMA 会读到垃圾数据导致 HardFault。
 *         重入安全由 OLED_DMA_Send 内部的 OLED_Wait_DMA() 串行化保证。
 *         ⚠️ 禁止在 ISR 中调用本函数（会死锁在 OLED_Wait_DMA）。
 */
void OLED_Write_Byte(uint8_t cmd, uint8_t mode){
    if(mode != CMD && mode != DATA) return;
    static uint8_t dma_buf;
    dma_buf = cmd;
    OLED_DMA_Send(mode, &dma_buf, 1);
}

//===============初始化环节===============

/**
 * @brief  OLED 屏幕初始化 (工业级防错版)
 * 配置为：OLED_WIDTH x OLED_HEIGHT 分辨率 + 水平寻址模式
 */
void OLED_Init(){
    // 1. 关闭显示（防止初始化配置期间屏幕出现乱码、花屏或闪烁）
    OLED_Write_Byte(0xAE,CMD); // 0xAE: 进入休眠模式 (Display OFF)

#if OLED_CONTROLLER == OLED_CONTROLLER_SSD1306
    // ==================【硬件滚动初始化】==================
    // 1.1 强制关闭所有硬件滚动
    // 避坑：如果是单片机软复位（没断电），OLED 的滚动引擎可能还在后台跑，
    // 此时往 RAM 里写数据会发生严重冲突，导致屏幕永久花屏或错位。
    OLED_Write_Byte(0x2E,CMD); // 0x2E: Deactivate scroll

    // 1.2 复位垂直滚动区域 (恢复为全屏 0~64)
    // 避坑：防止上一次程序运行残留的 0xA3 指令导致屏幕显示区域被诡异截断。
    OLED_Write_Byte(0xA3,CMD); // Set Vertical Scroll Area
    OLED_Write_Byte(0x00,CMD); // 顶部固定区域行数 = 0
    OLED_Write_Byte(OLED_HEIGHT, CMD); // 滚动区域行数 = 64 (全屏)
    // ==============================================================
#endif

    // 2. 优化时钟（调高芯片内部振荡器频率，使屏幕硬件层面的刷新极限拉到最高）
    OLED_Write_Byte(0xD5,CMD); // 0xD5: 设置显示时钟分频比与振荡器频率 (Set Display Clock Divide Ratio)
    OLED_Write_Byte(0xF0,CMD); // 0xF0: 高4位0xF将内部振荡器频推到最高，低4位0x0设分频比为1

    // 3. 设置驱动复用率（必须与屏幕实际物理行数精准匹配）
    OLED_Write_Byte(0xA8,CMD); // 0xA8: 设置多路复用率 (Set Multiplex Ratio)
    OLED_Write_Byte(OLED_HEIGHT - 1, CMD); // 复用率 = 行数 - 1

    // 4. 设置显示偏移与显示起点
    OLED_Write_Byte(0xD3,CMD); // 0xD3: 设置显示垂直偏移量 (Set Display Offset)
    OLED_Write_Byte(0x00,CMD); // 0x00: 偏移量设为0 (RAM的Row 0完美对应屏幕顶部的第一物理行)
    OLED_Write_Byte(0x40,CMD); // 0x40: 设置显示RAM的起始行地址为0 (Set Display Start Line)

    // 5. 配置为水平寻址模式
#if OLED_CONTROLLER == OLED_CONTROLLER_SSD1306
    OLED_Write_Byte(0x20,CMD); // 0x20: 设置内存寻址模式 (Set Memory Addressing Mode)
    OLED_Write_Byte(0x00,CMD); // SSD1306 horizontal addressing
#endif

    // 6. 限定寻址边界（将水平寻址的自动弹回窗口锁定在全屏范围内）
#if OLED_CONTROLLER == OLED_CONTROLLER_SSD1306
    OLED_Write_Byte(0x21,CMD);
    OLED_Write_Byte(OLED_COLUMN_OFFSET,CMD);
    OLED_Write_Byte(OLED_COLUMN_OFFSET + OLED_WIDTH - 1, CMD);
    OLED_Write_Byte(0x22,CMD);
    OLED_Write_Byte(0x00,CMD);
    OLED_Write_Byte(OLED_PAGES - 1, CMD);
#endif

    // 7. 翻转显示方向（默认 180° 镜像）
    OLED_Set_Mirror(OLED_DEFAULT_H_FLIP, OLED_DEFAULT_V_FLIP);

    // 8. COM硬件引脚及电气电平配置
    OLED_Write_Byte(0xDA,CMD); // 0xDA: 设置COM引脚硬件配置 (Set COM Pins Hardware Configuration)
    OLED_Write_Byte((OLED_HEIGHT > 32U) ? 0x12U : 0x02U, CMD);
    OLED_Write_Byte(0xDB,CMD); // 0xDB: 设置VCOMH取消选择电平 (Set VCOMH Deselect Level)
    OLED_Write_Byte(0x40,CMD); // 0x40: 对应约0.77*Vcc，使非选中行的截止更彻底，极大增强黑底纯净度

    // 9. 电气与发光性能调节（放电与对比度参数）
    OLED_Write_Byte(0xD9,CMD); // 0xD9: 设置预充电周期时长 (Set Pre-charge Period)
    OLED_Write_Byte(0xF1,CMD); // 0xF1: 确保极高刷新率下像素不拖影
    OLED_Write_Byte(0x81,CMD); // 0x81: 对比度控制 (Set Contrast Control)
    OLED_Write_Byte(0xCF,CMD); // 0xCF: 对比度数值设为0xCF (增大发光电流，使得图像明亮清晰)

    // 10. 全屏输出模式与正显配置
    OLED_Write_Byte(0xA4,CMD); // 0xA4: 输出遵循RAM内容 (Entire Display ON)
    OLED_Write_Byte(0xA6,CMD); // 0xA6: 设置正常显示模式 (Set Normal Display)

    // 11. 启动内置升压电荷泵（3.3V供电下必须开启此项屏幕才会亮）
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    OLED_Write_Byte(0xAD,CMD); // SH1106 DC-DC control
    OLED_Write_Byte(0x8B,CMD); // DC-DC ON
#else
    OLED_Write_Byte(0x8D,CMD); // SSD1306 charge pump setting
    OLED_Write_Byte(0x14,CMD); // charge pump ON
#endif

    // ==================【防雪花屏体验】==================
    // 在开机前，强制把单片机的空数组推送到 OLED 的显存里。
    // 避坑：OLED 刚通电时，硬件显存里全是随机噪点（雪花）。
    // 必须在 0xAF 开机指令前把它洗干净，否则开机瞬间屏幕会闪烁一下垃圾画面。
    OLED_GRAM_Clear();
    OLED_GRAM_Refresh();
    OLED_Wait_DMA(); // 必须阻塞等待这帧黑屏数据传完
    // ==============================================================

    // 12. 正式开机
    OLED_Write_Byte(0xAF,CMD); // 0xAF: 开启OLED面板显示 (Display ON)
    OLED_Wait_DMA();
}
//===============函数实现区域===============
/**
 * @brief  将显存 OLED_GRAM 整帧刷新到屏幕（DMA）
 */
void OLED_GRAM_Refresh(){
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    static uint8_t page_cmd[3];
    for (uint8_t page = 0U; page < OLED_PAGES; ++page) {
        page_cmd[0] = (uint8_t)(0xB0U | page);
        page_cmd[1] = (uint8_t)(OLED_COLUMN_OFFSET & 0x0FU);
        page_cmd[2] = (uint8_t)(0x10U | ((OLED_COLUMN_OFFSET >> 4U) & 0x0FU));
        (void)OLED_DMA_Send(CMD, page_cmd, sizeof(page_cmd));
        OLED_Wait_DMA();
        (void)OLED_DMA_Send(DATA, OLED_GRAM[page], OLED_WIDTH);
    }
#else
    OLED_DMA_Send(DATA, OLED_GRAM[0], OLED_GRAM_SIZE);
#endif
}

#if OLED_USE_DOUBLE_BUFFER
static void OLED_Copy_Buffer_To_Front(const uint8_t (*source)[OLED_WIDTH])
{
    if (source == OLED_GRAM) return;
    OLED_Wait_DMA();
    (void)memcpy(OLED_GRAM, source, sizeof(OLED_GRAM));
}

static void OLED_Present_Buffer(const uint8_t (*source)[OLED_WIDTH])
{
    OLED_Copy_Buffer_To_Front(source);
    OLED_GRAM_Refresh();
}
#endif

// ==================== 双缓冲管理 ====================
#if OLED_USE_DOUBLE_BUFFER

/**
 * @brief  选择当前绘图目标缓冲区
 * @param  buffer_id  0 = 前台（OLED_GRAM），1 = 后台（OLED_BACK_BUFFER）
 * @note   - 仅修改 draw_buffer 指针指向，不搬运任何数据。
 *         - 选中后所有后续绘图操作（清屏、画点、位图、文本等）
 *           均写入所选缓冲区。
 *         - 绘制完毕后调用 OLED_Swap_Buffers() 将后台内容推送到屏幕。
 */
void OLED_Select_Buffer(uint8_t buffer_id)
{
    if (buffer_id == 0u) {
        draw_buffer         = OLED_GRAM;
        g_current_buffer_id = 0u;
    } else if (buffer_id == 1u) {
        draw_buffer         = OLED_BACK_BUFFER;
        g_current_buffer_id = 1u;
    }
    /* 其他 buffer_id 值：不做任何操作 */
}

/**
 * @brief  交换前后台缓冲区并刷新屏幕
 * @note   1. 将后台缓冲区 OLED_BACK_BUFFER 的 1 KB 内容拷贝至前台 OLED_GRAM。
 *         2. 调用 OLED_GRAM_Refresh() 将新前台内容显示到屏幕。
 *         3. 保持 draw_buffer 指向后台缓冲区，下一帧可直接绘制无需再次 Select。
 *
 *         性能说明：
 *         1 KB 的 memcpy 在 Cortex-M 上约几微秒（72 MHz 约 10~20 μs），
 *         远小于 I2C DMA 传输时间（~几毫秒），在绝大多数场景下不构成性能瓶颈。
 *         若在 ISR 或极高帧率场景下需进一步优化，可改用 DMA 进行内存搬运。
 */
void OLED_Swap_Buffers(void)
{
    OLED_Present_Buffer(OLED_BACK_BUFFER);
    draw_buffer         = OLED_BACK_BUFFER;
    g_current_buffer_id = 1u;
}

#endif /* OLED_USE_DOUBLE_BUFFER */

/**
 * @brief  计算并返回屏幕当前帧率（FPS），精度保留至 .2f，实际上是两次调用之间的时间间隔
 * @note   每次调用本函数即计一帧，因此应紧跟在 OLED_GRAM_Refresh() 之后调用，
 *         或在使用局部刷新时在 OLED_Refresh_Rect() 之后调用。
 *         - 内部每秒做一次 float 除法更新缓存值，其余时间零浮点开销。
 *         - 超过 1 秒无新帧则自动归零，防止长时间空闲后的过时数据。
 *         - 全程指针访问静态结构体，单周期 ldr/str，无重定位开销。
 *         - 单位： ms
 * @retval 当前帧率（帧/秒），如 60.00
 */
float OLED_Calc_FPS(void)
{
    // 帧率统计上下文 —— 静态变量，指针聚合访问以压缩指令
    static struct {
        uint32_t cnt;   // 自上次采样以来的刷新帧累计
        uint32_t mark;  // 上一采样时刻（端口层毫秒时钟）
        float   val;   // 最近一次有效的 FPS 计算结果
    } s;

    uint32_t now = s_port.tick_ms != NULL ? s_port.tick_ms(s_port.context) : 0U;
    uint32_t dt  = now - s.mark;           // 距上次采样的毫秒增量

    s.cnt++;                                // 本帧计数

    // —— 满 1 秒窗口后刷新 FPS ——
    if (dt >= 1000u) {
        // FPS = 总帧数 / (总毫秒 / 1000)
        // 即：帧数 × 1000 ÷ 毫秒
        s.val  = static_cast<float>(s.cnt) * 1000.0f /
                 static_cast<float>(dt);
        s.cnt  = 0;
        s.mark = now;
    }

    return s.val;
}

/**
 * @brief  整数版 FPS 计算（无浮点运算，适合无 FPU 芯片）
 * @retval 当前帧率（帧/秒），整数精度，如 60
 * @note   使用独立于 OLED_Calc_FPS 的计数器，两者只需调用其中一个。
 */
uint16_t OLED_Calc_FPS_Int(void)
{
    static struct {
        uint32_t cnt;
        uint32_t mark;
        uint16_t val;
    } s;

    uint32_t now = s_port.tick_ms != NULL ? s_port.tick_ms(s_port.context) : 0U;
    uint32_t dt  = now - s.mark;

    s.cnt++;

    if (dt >= 1000u) {
        s.val  = static_cast<uint16_t>((s.cnt * 1000u) / dt);
        s.cnt  = 0;
        s.mark = now;
    }

    return s.val;
}

/**
 * @brief  将显存 OLED_GRAM 全部清零（不刷新屏幕）
 */
void OLED_GRAM_Clear(){
    memset(draw_buffer[0], 0x00, OLED_GRAM_SIZE);
}

/**
 * @brief  将显存所有像素置为点亮状态（不刷新屏幕），用于坏点检测
 * @note   调用后需 OLED_GRAM_Refresh() 将数据推到屏幕方可观察
 */
void OLED_GRAM_Fill(void){
    memset(draw_buffer[0], 0xFF, OLED_GRAM_SIZE);
}

/**
 * @brief  清空显存并刷新屏幕（组合 Clear + Refresh）
 */
void OLED_Clear(){
    OLED_GRAM_Clear();
#if OLED_USE_DOUBLE_BUFFER
    OLED_Present_Buffer(draw_buffer);
#else
    OLED_GRAM_Refresh();
#endif
}

/**
 * @brief  进入低功耗休眠（关闭显示和电荷泵）
 */
void OLED_Sleep(){
    OLED_Write_Byte(0xAE, CMD);          // 关闭显示
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    OLED_Write_Byte(0xAD, CMD);
    OLED_Write_Byte(0x8A, CMD);
#else
    OLED_Write_Byte(0x8D, CMD);          // 电荷泵设置
    OLED_Write_Byte(0x10, CMD);          // 关闭电荷泵
#endif
}

/**
 * @brief  从休眠中唤醒（开启电荷泵和显示）
 */
void OLED_Wake(){
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    OLED_Write_Byte(0xAD, CMD);
    OLED_Write_Byte(0x8B, CMD);
#else
    OLED_Write_Byte(0x8D, CMD);          // 电荷泵设置
    OLED_Write_Byte(0x14, CMD);          // 开启电荷泵
#endif
    OLED_Write_Byte(0xAF, CMD);          // 开启显示
}

/**
 * @brief  设置 OLED 对比度
 * @param  level: 对比度值 (0~255)，越大越亮
 */
void OLED_Set_Contrast(uint8_t level){
    OLED_Write_Byte(0x81, CMD);
    OLED_Write_Byte(level, CMD);
}

/**
 * @brief  设置屏幕镜像方向
 * @param  h_flip: 1=水平翻转, 0=正常
 * @param  v_flip: 1=垂直翻转, 0=正常
 * @param  注意此时经过屏幕初始化之后的坐标为正常的，但是已经h_flip=1 v_flip=1，此时想实现翻转就得取0
 */
void OLED_Set_Mirror(uint8_t h_flip, uint8_t v_flip){
    OLED_Write_Byte(0xA0 | (h_flip ? 0x01 : 0x00), CMD);
    OLED_Write_Byte(0xC0 | (v_flip ? 0x08 : 0x00), CMD);
}

/**
 * @brief  设置反显模式（亮灭互换，无需修改显存）
 * @param  inverse: 0=正常显示(0xA6), 非0=反显(0xA7)
 */
void OLED_Set_Inverse(uint8_t inverse){
    OLED_Write_Byte(inverse ? 0xA7 : 0xA6, CMD);
}

/**
 * @brief  软件局部反显 —— 将指定矩形区域内的显存像素逐位取反（亮↔灭）
 * @param  x0: 矩形左上角 X（可正可负）
 * @param  y0: 矩形左上角 Y（可正可负）
 * @param  dx: 水平跨度（正=向右，负=向左）
 * @param  dy: 垂直跨度（正=向下，负=向上）
 * @note   自动与屏幕边界取交集；无交集时直接返回。
 *         直接操作当前绘图缓冲区（兼容双缓冲）。
 *         ⚠️ 不支持屏幕旋转坐标变换（同 OLED_Clear_Rect）。
 */
void OLED_SW_Invert_Rect(int16_t x0, int16_t y0, int16_t dx, int16_t dy)
{
    OLED_ClippedRect rect;
    if (!OLED_Clip_Physical_Rect(x0, y0, dx, dy, &rect)) return;

    const uint8_t pg_top = (uint8_t)(rect.top >> 3);
    const uint8_t pg_bot = (uint8_t)((rect.bottom - 1U) >> 3);
    const uint8_t xs = (uint8_t)rect.left;
    const uint8_t cols = (uint8_t)(rect.right - rect.left);

    if (pg_top == pg_bot) {
        uint8_t mask = (uint8_t)((0xFF << (rect.top & 0x07U)) &
                                 (0xFF >> (7U - ((rect.bottom - 1U) & 0x07U))));
        uint8_t* p = DRAW_BUFFER(pg_top) + xs;
        uint8_t* end = p + cols;
        while (p < end) { *p ^= mask; p++; }
    } else {
        {
            uint8_t mask = (uint8_t)(0xFF << (rect.top & 0x07U));
            uint8_t* p = DRAW_BUFFER(pg_top) + xs;
            uint8_t* end = p + cols;
            while (p < end) { *p ^= mask; p++; }
        }
        for (uint8_t pg = pg_top + 1; pg < pg_bot; pg++) {
            uint8_t* p = DRAW_BUFFER(pg) + xs;
            uint8_t* end = p + cols;
            while (p < end) { *p ^= 0xFF; p++; }
        }
        {
            uint8_t mask = (uint8_t)(0xFF >> (7U - ((rect.bottom - 1U) & 0x07U)));
            uint8_t* p = DRAW_BUFFER(pg_bot) + xs;
            uint8_t* end = p + cols;
            while (p < end) { *p ^= mask; p++; }
        }
    }
}

/**
 * @brief  检测 OLED 是否在 I2C 总线上就绪
 * @retval OLED_PORT_OK 表示设备正常，其他 OLED_PORT_* 表示错误
 * @param  该函数在设备多的情况下可以配合状态机使用
 */
int OLED_Detect(void){
    if (s_port.device_ready == NULL) return OLED_PORT_NOT_BOUND;
    return s_port.device_ready(s_port.context, OLED_I2C_ADDRESS, 3U, 100U);
}

/**
 * @brief  导出显存到用户缓冲区（可用于截图或动画帧缓存）
 * @param  dest: 目标缓冲区指针，需至少 OLED_GRAM_SIZE 字节
 */
void OLED_Export_GRAM(uint8_t* dest){
    if (!dest) return;
    memcpy(dest, OLED_GRAM[0], OLED_GRAM_SIZE);
}

/**
 * @brief  从用户缓冲区导入数据到当前绘图缓冲区
 * @param  src: 源缓冲区指针，需至少 OLED_GRAM_SIZE 字节
 * @note   导入目标为 draw_buffer（双缓冲模式下跟随 Select_Buffer 选择）。
 *         导入后需调用 OLED_GRAM_Refresh() 或 OLED_Swap_Buffers() 显示。
 */
void OLED_Import_GRAM(const uint8_t* src){
    if (!src) return;
    memcpy(draw_buffer[0], src, OLED_GRAM_SIZE);
}

/**
 * @brief  硬件层面连续垂直+水平组合滚动设置（SSD1306）
 * @param  dir:      水平滚动方向 0=向右+垂直(0x29), 非0=向左+垂直(0x2A)
 * @param  start_pg: 滚动起始页 (0~OLED_PAGES-1)
 * @param  end_pg:   滚动结束页 (0~OLED_PAGES-1)，硬件要求 end_pg >= start_pg
 * @param  speed:    滚动速度 (0~7)，数值越小滚动越快（具体映射见数据手册）
 * @param  offset:   垂直滚动偏移行数 (1~0x3F)，每帧垂直偏移量
 * @note   【重要】调用此函数后，必须调用 OLED_Scroll_HW_Switch(1) 激活滚动。
 *        激活滚动后严禁再向 GDDRAM 写入数据（包括调用 OLED_GRAM_Refresh），
 *        否则滚动引擎会失效或花屏。停止滚动后需重写显存并刷新。
 *         *   speed 速度字典：
 *             0=5帧  1=64帧 2=128帧 3=256帧
 *             4=3帧  5=4帧  6=25帧  7=2帧  (帧数越少滚动越快)
 */
void OLED_Scroll_HW_HV(uint8_t dir, uint8_t start_pg, uint8_t end_pg,
                       uint8_t speed, uint8_t offset)
{
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    (void)dir;
    (void)start_pg;
    (void)end_pg;
    (void)speed;
    (void)offset;
    return;
#else
    // 参数有效性检查
    if (start_pg >= OLED_PAGES || end_pg >= OLED_PAGES) return;
    if (end_pg < start_pg) return;
    if (speed > 7) return;
    if (offset < 1 || offset > 0x3F) return;

    // ========== 发送滚动配置（29h/2Ah 命令，8 字节）==========
    uint8_t cmd_seq[8];
    uint8_t* p = cmd_seq;
    *p++ = dir ? 0x2A : 0x29;   // 方向字节
    *p++ = 0x01;                // bit0=1 使能水平分量，纯0只垂直
    *p++ = start_pg & 0x07;     // 起始页
    *p++ = speed & 0x07;        // 速度
    *p++ = end_pg & 0x07;       // 结束页
    *p++ = offset & 0x3F;       // 垂直偏移
    *p++ = 0x00;
    *p   = 0xFF;
    (void)OLED_DMA_Send(CMD, cmd_seq, sizeof(cmd_seq));
    OLED_Wait_DMA();
    // 注意：这里不自动激活滚动，由用户手动调用 OLED_Scroll_HW_Switch(1)
#endif
}



/**
 * @brief  硬件水平连续滚动设置（SSD1306 内置硬件滚动引擎，不占 CPU）
 * @param  dir:      滚动方向 0=右滚(0x26), 非0=左滚(0x27)
 * @param  start_pg: 滚动起始页 (0~7)
 * @param  end_pg:   滚动结束页 (0~7)，硬件要求 D >= B
 * @param  speed:    滚动速度帧间隔 (0~7)，见 speed 字典
 * @note   仅下发配置，不激活。需调用 OLED_Scroll_HW_Switch(1, mode) 启动滚动。
 *
 *   speed 速度字典：
 *   0=5帧  1=64帧 2=128帧 3=256帧
 *   4=3帧  5=4帧  6=25帧  7=2帧  (帧数越少滚动越快)
 *   啥阴啊，这个滚动搞了我一天，CSDN本来有文章说这个的，结果....VIP
 */
void OLED_Scroll_HW_H(uint8_t dir, uint8_t start_pg, uint8_t end_pg, uint8_t speed){
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    (void)dir;
    (void)start_pg;
    (void)end_pg;
    (void)speed;
    return;
#else
    if (start_pg >= OLED_PAGES || end_pg >= OLED_PAGES) return;
    if (end_pg < start_pg) return;
    if (speed > 7) return;

    uint8_t cmd_seq[7];
    uint8_t* p = cmd_seq;
    *p++ = dir ? 0x27 : 0x26;
    *p++ = 0x00;
    *p++ = start_pg & 0x07;
    *p++ = speed & 0x07;
    *p++ = end_pg & 0x07;
    *p++ = 0x00;
    *p   = 0xFF;

    (void)OLED_DMA_Send(CMD, cmd_seq, sizeof(cmd_seq));
    OLED_Wait_DMA();
#endif
}

/**
 * @brief  硬件层面滚动开启/停止控制
 * @note   删除了多余的 mode 参数，纯粹负责发送 0x2F (开启) 或 0x2E (关闭)
 */
void OLED_Scroll_HW_Switch(uint8_t enable)
{
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    (void)enable;
#else
    static uint8_t cmd;
    cmd = enable ? 0x2F : 0x2E;
    (void)OLED_DMA_Send(CMD, &cmd, 1U);
#endif
}

// ==================== 屏幕旋转 ====================
uint8_t g_oled_rotation = OLED_ROT_0;

/* 逻辑屏幕尺寸——90°/270° 时宽高互换，供内部裁剪使用 */
#define OLED_LOG_W  ((g_oled_rotation & 1u) ? OLED_HEIGHT : OLED_WIDTH)
#define OLED_LOG_H  ((g_oled_rotation & 1u) ? OLED_WIDTH  : OLED_HEIGHT)

/**
 * @brief  设置屏幕逻辑旋转角度
 * @param  rotation: OLED_ROT_0 / OLED_ROT_90 / OLED_ROT_180 / OLED_ROT_270
 * @note   仅修改全局变量，不搬移显存。设置后所有绘图自动应用新方向。
 */
void OLED_Set_Rotation(uint8_t rotation){
    if (rotation <= OLED_ROT_270) g_oled_rotation = rotation;
}

// ==================== 内部性能宏 ====================

/* 无边界检查的极速打点（仅 ROT_0，调用方保证坐标合法） */
#define OLED_DRAW_POINT_FAST(x, y) \
    (DRAW_BUFFER((y) >> 3)[(x)] |= (1u << ((y) & 0x07)))

/* 无边界检查的水平线段批量填充（直接操作显存，ROT_0 专用）
 * xa, xb: 列范围 [xa, xb]（闭区间），row: 行坐标
 * 调用方保证坐标在当前 OLED_WIDTH/OLED_HEIGHT 内。 */
static inline void OLED_HLine_Fast(uint8_t xa, uint8_t xb, uint8_t row)
{
    uint8_t pg   = row >> 3;
    uint8_t bit  = (uint8_t)(1u << (row & 0x07));
    uint8_t* p   = DRAW_BUFFER(pg) + xa;
    uint8_t* end = DRAW_BUFFER(pg) + xb + 1;
    while (p < end) { *p |= bit; p++; }
}

/* 批量矩形填充（直接操作显存，ROT_0 专用）
 * 对 [xs, xe] × [ys, ye] 闭区间置位
 * 调用方保证坐标在当前 OLED_WIDTH/OLED_HEIGHT 内。 */
static inline void OLED_Fill_Rect_Fast(uint8_t xs, uint8_t xe, uint8_t ys, uint8_t ye)
{
    uint8_t pg_top = ys >> 3;
    uint8_t pg_bot = ye >> 3;
    uint16_t cols = (uint16_t)((uint16_t)xe - xs + 1U);

    if (pg_top == pg_bot) {
        uint8_t mask = (uint8_t)((0xFF << (ys & 0x07)) & (0xFF >> (7 - (ye & 0x07))));
        uint8_t* p = DRAW_BUFFER(pg_top) + xs;
        uint8_t* end = p + cols;
        while (p < end) { *p |= mask; p++; }
    } else {
        {
            uint8_t mask = (uint8_t)(0xFF << (ys & 0x07));
            uint8_t* p = DRAW_BUFFER(pg_top) + xs;
            uint8_t* end = p + cols;
            while (p < end) { *p |= mask; p++; }
        }
        for (uint8_t pg = pg_top + 1; pg < pg_bot; pg++) {
            memset(DRAW_BUFFER(pg) + xs, 0xFF, cols);
        }
        {
            uint8_t mask = (uint8_t)(0xFF >> (7 - (ye & 0x07)));
            uint8_t* p = DRAW_BUFFER(pg_bot) + xs;
            uint8_t* end = p + cols;
            while (p < end) { *p |= mask; p++; }
        }
    }
}

/**
 * @brief  在 OLED_GRAM 中点亮指定像素（不刷新屏幕）
 * @param  x: 逻辑列坐标（旋转后坐标系）
 * @param  y: 逻辑行坐标（旋转后坐标系）
 * @note   内部根据 g_oled_rotation 做坐标变换后写入物理显存。
 *         0° 时无额外开销（单次比较 + 预测分支）。
 */
void OLED_Draw_Point(uint8_t x, uint8_t y){
    uint8_t rx, ry;

    if (g_oled_rotation == OLED_ROT_0) {
        if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
        DRAW_BUFFER(y >> 3)[x] |= (1u << (y & 0x07));
        return;
    }

    /* 慢速路径：坐标变换 */
    switch (g_oled_rotation) {
        case OLED_ROT_90:   /* 逻辑宽高互换 */
            if (x >= OLED_HEIGHT || y >= OLED_WIDTH) return;
            rx = (uint8_t)(OLED_WIDTH - 1 - y);
            ry = x;
            break;
        case OLED_ROT_180:
            if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
            rx = (uint8_t)(OLED_WIDTH - 1 - x);
            ry = (uint8_t)(OLED_HEIGHT - 1 - y);
            break;
        case OLED_ROT_270:  /* 逻辑宽高互换 */
            if (x >= OLED_HEIGHT || y >= OLED_WIDTH) return;
            rx = y;
            ry = (uint8_t)(OLED_HEIGHT - 1 - x);
            break;
        default: return;
    }
    DRAW_BUFFER(ry >> 3)[rx] |= (1u << (ry & 0x07));
}

/**
 * @brief  在 OLED_GRAM 中绘制位图（不刷新屏幕），直接按字节操作，性能远高于逐像素绘制
 * @param  x: 位图左上角屏幕列坐标，支持负数（部分超出屏幕左侧时自动裁剪）
 * @param  y: 位图左上角屏幕行坐标，支持负数（部分超出屏幕顶部时自动裁剪）
 * @param  bmp_width:  位图宽度（像素），必须 > 0
 * @param  bmp_height: 位图高度（像素），必须 > 0
 * @param  bmp_data:   位图数据指针，列行式取模格式：
 *                     数据按页组织 [页0][列0..宽-1] [页1][列0..宽-1] ...，
 *                     适配取模软件配置：阴码 + 列行式 + 顺向 + C51格式
 * @note   1. 位图超出屏幕区域时自动裁剪，仅绘制与屏幕有交集的部分
 *         2. 若整张位图均不在当前 OLED_WIDTH/OLED_HEIGHT 内，直接返回
 *         3. 本函数使用"读-修改-写"方式仅改写位图覆盖的位，不破坏相邻像素
 *         4. 如需位图的 0 像素也强制清除屏幕原有内容，请在调用前使用 OLED_Clear_Rect
 *         ⚠️ 本函数直接按字节写入物理显存，不经过 OLED_Draw_Point，
 *            因此 **不受 OLED_Set_Rotation 旋转影响**。
 *            旋转模式下请使用物理坐标调用，或改用逐点绘制方式。
 */
void OLED_Draw_Bitmap(int16_t x, int16_t y,
                      uint8_t bmp_width, uint8_t bmp_height,
                      const uint8_t* bmp_data)
{
    // ========== 1. 参数有效性检查 ==========
    if (!bmp_data || bmp_width == 0 || bmp_height == 0) return;
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;

    // ========== 2. 裁剪到屏幕可见区域 ==========
    // 列方向裁剪（支持负数坐标，仅保留与 [0, OLED_WIDTH) 有交集的部分）
    int32_t x1 = (int32_t)x + bmp_width - 1;        // 位图右下角列坐标
    if (x1 < 0 || x >= OLED_WIDTH) return;           // 整张位图在屏幕左/右侧之外
    uint8_t col_start = (x < 0) ? 0 : (uint8_t)x;
    uint8_t col_end   = (x1 >= OLED_WIDTH) ? (uint8_t)(OLED_WIDTH - 1) : (uint8_t)x1;

    // 行方向裁剪（支持负数坐标，仅保留与 [0, OLED_HEIGHT) 有交集的部分）
    int32_t y1 = (int32_t)y + bmp_height - 1;        // 位图右下角行坐标
    if (y1 < 0 || y >= OLED_HEIGHT) return;           // 整张位图在屏幕上方/下方之外
    uint8_t row_start = (y < 0) ? 0 : (uint8_t)y;
    uint8_t row_end   = (y1 >= OLED_HEIGHT) ? (uint8_t)(OLED_HEIGHT - 1) : (uint8_t)y1;

    // 交集为空（理论上前面已排除，这里作为安全网）
    if (col_start > col_end || row_start > row_end) return;

    uint16_t vis_cols = static_cast<uint16_t>(col_end) - col_start + 1U;
    uint8_t pg_start = row_start >> 3;             // 可见区域起始页号
    uint8_t pg_end   = row_end   >> 3;             // 可见区域结束页号
    uint16_t src_off = static_cast<uint16_t>(static_cast<int32_t>(col_start) - x);


    // ========== 4. 逐屏幕页绘制 ==========
    for (uint8_t pg = pg_start; pg <= pg_end; pg++) {

        // ---- 4a. 计算该页的位掩码 ----
        // 掩码决定了该页 8 位中哪些位属于位图可见区域
        // 首/末页可能只覆盖部分位，中间页覆盖全部 8 位
        uint8_t first_bit = (pg == pg_start) ? (row_start & 0x07) : 0;
        uint8_t last_bit  = (pg == pg_end)   ? (row_end   & 0x07) : 7;
        uint8_t mask = (uint8_t)((0xFFu << first_bit) & (0xFFu >> (7 - last_bit)));

        // ---- 4b. 计算该页对应的源位图行范围 ----
        // 屏幕页 pg 覆盖的屏幕行 → 映射回源位图的行索引
        uint8_t img_start, img_end;
        if (pg == pg_start)
            img_start = static_cast<uint8_t>(static_cast<int32_t>(row_start) - y);
        else
            img_start = (uint8_t)((pg << 3) - y);       // 该页第 0 行对应位图第几行

        if (pg == pg_end)
            img_end = static_cast<uint8_t>(static_cast<int32_t>(row_end) - y);
        else
            img_end = (uint8_t)((pg << 3) + 7 - y);     // 该页第 7 行对应位图第几行

        // 确定这些行分布在哪（几）个源页中
        uint8_t sp_lo = img_start >> 3;        // 低位源页索引
        uint8_t sp_hi = img_end   >> 3;        // 高位源页索引
        uint8_t shift = img_start & 0x07;      // 源字节需右移多少位才能对齐到 bit0

        // 指向低位源页中对应可见首列的数据
        const uint8_t* src_lo = bmp_data + (uint16_t)sp_lo * bmp_width + src_off;
        uint8_t* dst = &DRAW_BUFFER(pg)[col_start];

        if (sp_lo == sp_hi) {
            // ---- 情况 A: 所有位来自同一个源页 ----
            for (uint16_t c = 0; c < vis_cols; c++) {
                // 从源字节取出有效位，右移对齐 bit0，再左移对齐目标页
                uint8_t data = (uint8_t)(src_lo[c] >> shift) << first_bit;
                // 读-修改-写：只改位图区域，保护相邻像素
                dst[c] = (dst[c] & ~mask) | (data & mask);
            }
        } else {
            // ---- 情况 B: 跨两个源页，拼接两个源字节 ----
            const uint8_t* src_hi = bmp_data + (uint16_t)sp_hi * bmp_width + src_off;
            uint8_t shl = 8 - shift;
            for (uint16_t c = 0; c < vis_cols; c++) {
                // 低位源页的高位 + 高位源页的低位 → 拼接为完整的 8 位数据
                uint8_t data = (uint8_t)(src_lo[c] >> shift)
                             | (uint8_t)(src_hi[c] << shl);
                data = (uint8_t)(data << first_bit);
                dst[c] = (dst[c] & ~mask) | (data & mask);
            }
        }
    }

}

/**
 * @brief  在 OLED_GRAM 中逐像素绘制 ASCII 字符（不刷新屏幕）
 * @param  tmp:  要显示的 ASCII 字符
 * @param  size: 字号 "0806"/"1206"/"1608"/"2412"
 * @param  x:    起始列坐标 (0 ~ OLED_WIDTH-1)
 * @param  y:    起始行坐标 (0 ~ OLED_HEIGHT-1)
 */
void OLED_Show_Char_ASCII(char tmp, const char* size, uint8_t x, uint8_t y){
    if (size == NULL || x >= OLED_LOG_W || y >= OLED_LOG_H) return;
    if (tmp < ' ' || tmp > '~') return;

    const uint8_t* font;
    uint8_t width, pages, char_h;
    switch (size[0]) {
        case '0':
            width = 6; pages = 1; char_h = 8;
            font = asc2_0806[tmp - ' '];
            break;
        case '1':
            if (size[1] == '2') {
                width = 6; pages = 2; char_h = 12;
                font = asc2_1206[tmp - ' '];
            } else {
                width = 8; pages = 2; char_h = 16;
                font = asc2_1608[tmp - ' '];
            }
            break;
        case '2':
            width = 12; pages = 3; char_h = 24;
            font = asc2_2412[tmp - ' '];
            break;
        default: return;
    }

    /* ROT_0 快速路径：直接按字节写入显存，绕过 OLED_Draw_Point 的逐点开销 */
    if (g_oled_rotation == OLED_ROT_0) {
        uint8_t shift = y & 0x07;
        uint8_t pg_base = y >> 3;
        uint16_t col_end = (uint16_t)x + width;
        if (col_end > OLED_WIDTH) col_end = OLED_WIDTH;

        for (uint8_t col = 0; col < width && ((uint16_t)x + col) < col_end; col++) {
            uint8_t px = (uint8_t)((uint16_t)x + col);
            for (uint8_t pg = 0; pg < pages; pg++) {
                uint8_t byte = font[pg * width + col];
                if (!byte) continue;
                uint8_t rows = (pg == pages - 1 && (char_h & 0x07))
                               ? (char_h & 0x07) : 8;
                if (rows < 8) byte &= (uint8_t)((1u << rows) - 1u);

                uint8_t dst_pg = pg_base + pg;
                if (shift == 0) {
                    if (dst_pg < OLED_PAGES)
                        DRAW_BUFFER(dst_pg)[px] |= byte;
                } else {
                    if (dst_pg < OLED_PAGES)
                        DRAW_BUFFER(dst_pg)[px] |= (uint8_t)(byte << shift);
                    if (dst_pg + 1 < OLED_PAGES)
                        DRAW_BUFFER(dst_pg + 1)[px] |= (uint8_t)(byte >> (8 - shift));
                }
            }
        }
        return;
    }

    /* 慢速路径：支持旋转 */
    for (uint8_t col = 0; col < width; col++) {
        uint8_t px = x + col;
        if (px >= OLED_LOG_W) break;

        for (uint8_t pg = 0; pg < pages; pg++) {
            uint8_t byte = font[pg * width + col];
            if (!byte) continue;

            uint8_t rows = (pg == pages - 1 && (char_h & 0x07))
                           ? (char_h & 0x07) : 8;
            uint16_t base = (uint16_t)((uint16_t)y + ((uint16_t)pg << 3));

            while (byte) {
                uint8_t b = OLED_CTZ8(byte);
                if (b >= rows) break;
                uint16_t py = base + b;
                if (py >= OLED_LOG_H) break;
                OLED_Draw_Point(px, (uint8_t)py);
                byte &= byte - 1;
            }
        }
    }
}

/**
 * @brief  在 OLED_GRAM 中逐像素绘制字符串（不刷新屏幕）
 * @param  str:  要显示的字符串（以 '\0' 结尾）
 * @param  size: 字号 "0806"/"1206"/"1608"/"2412"
 * @param  x:    字符串左上角起始列坐标 (0 ~ OLED_WIDTH-1)
 * @param  y:    字符串左上角起始行坐标 (0 ~ OLED_HEIGHT-1)
 */
void OLED_Show_String(const char* str, const char* size, uint8_t x, uint8_t y){
    if (str == NULL || size == NULL || x >= OLED_LOG_W || y >= OLED_LOG_H) return;

    uint8_t step;
    switch (size[0]) {
        case '0': step = 6; break;
        case '1': step = (size[1] == '2') ? 6 : 8; break;
        case '2': step = 12; break;
        default:  return;
    }

    while (*str) {
        if (x + step > OLED_LOG_W) break;
        OLED_Show_Char_ASCII(*str++, size, x, y);
        x += step;
    }
}

/**
 * @brief  在 OLED_GRAM 中绘制线段 / 直线（不刷新屏幕）
 * @param  x0:   起始列坐标 (signed)
 * @param  y0:   起始行坐标 (signed)
 * @param  dx:   x 增量，正右负左
 * @param  dy:   y 增量，正下负上
 * @param  mode: 0=有限线段（两端必须完整在屏内）；1=无限直线（自动裁剪至屏幕边界）
 */
void OLED_Draw_Line(int16_t x0, int16_t y0, int16_t dx, int16_t dy, uint8_t mode){
    const int32_t lw = OLED_LOG_W;
    const int32_t lh = OLED_LOG_H;
    const int32_t start_x = x0;
    const int32_t start_y = y0;
    const int32_t delta_x = dx;
    const int32_t delta_y = dy;

    if (mode == 0) {
        if (start_x < 0 || start_x >= lw || start_y < 0 || start_y >= lh) return;
        const int32_t x1 = start_x + delta_x;
        const int32_t y1 = start_y + delta_y;
        if (x1 < 0 || x1 >= lw || y1 < 0 || y1 >= lh) return;

        const int32_t adx = delta_x < 0 ? -delta_x : delta_x;
        const int32_t ady = delta_y < 0 ? -delta_y : delta_y;
        if (adx == 0 && ady == 0) {
            OLED_Draw_Point((uint8_t)start_x, (uint8_t)start_y);
            return;
        }

        const int32_t sx = delta_x > 0 ? 1 : (delta_x < 0 ? -1 : 0);
        const int32_t sy = delta_y > 0 ? 1 : (delta_y < 0 ? -1 : 0);

        int32_t x = start_x;
        int32_t y = start_y;
        if (adx >= ady) {
            int32_t error = (ady << 1) - adx;
            while (1) {
                OLED_Draw_Point((uint8_t)x, (uint8_t)y);
                if (x == x1) break;
                x += sx;
                if (error > 0) { y += sy; error -= (adx << 1); }
                error += (ady << 1);
            }
        } else {
            int32_t error = (adx << 1) - ady;
            while (1) {
                OLED_Draw_Point((uint8_t)x, (uint8_t)y);
                if (y == y1) break;
                y += sy;
                if (error > 0) { x += sx; error -= (ady << 1); }
                error += (adx << 1);
            }
        }
        return;
    }

    const int32_t adx = delta_x < 0 ? -delta_x : delta_x;
    const int32_t ady = delta_y < 0 ? -delta_y : delta_y;
    if (adx == 0 && ady == 0) return;

    if (adx >= ady) {
        for (int32_t x = 0; x < lw; ++x) {
            const int32_t y = start_y +
                OLED_Round_Divide((x - start_x) * delta_y, delta_x);
            if (y >= 0 && y < lh) OLED_Draw_Point((uint8_t)x, (uint8_t)y);
        }
    } else {
        for (int32_t y = 0; y < lh; ++y) {
            const int32_t x = start_x +
                OLED_Round_Divide((y - start_y) * delta_x, delta_y);
            if (x >= 0 && x < lw) OLED_Draw_Point((uint8_t)x, (uint8_t)y);
        }
    }
}

/**
 * @brief  在 OLED_GRAM 中绘制矩形（不刷新屏幕）
 * @param  x:    起点列坐标（矩形左上角）
 * @param  y:    起点行坐标（矩形左上角）
 * @param  dx:   矩形宽度，正右负左
 * @param  dy:   矩形高度，正下负上
 * @param  mode: 0=仅边框；1=填充
 * @note   起点或终点超出屏幕则直接返回。内部调用 OLED_Draw_Line。
 */
void OLED_Draw_Rectang(int16_t x, int16_t y, int16_t dx, int16_t dy, uint8_t mode)
{
    const int32_t lw = OLED_LOG_W;
    const int32_t lh = OLED_LOG_H;

    if (x < 0 || x >= lw || y < 0 || y >= lh) return;

    const int32_t x1 = static_cast<int32_t>(x) + dx;
    const int32_t y1 = static_cast<int32_t>(y) + dy;

    if (x1 < 0 || x1 >= lw || y1 < 0 || y1 >= lh) return;

    if (mode == 0) {
        OLED_Draw_Line(x,  y,  dx, 0,  0);
        OLED_Draw_Line(static_cast<int16_t>(x1), y, 0, dy, 0);
        OLED_Draw_Line(static_cast<int16_t>(x1), static_cast<int16_t>(y1),
                       static_cast<int16_t>(-static_cast<int32_t>(dx)), 0, 0);
        OLED_Draw_Line(x, static_cast<int16_t>(y1), 0,
                       static_cast<int16_t>(-static_cast<int32_t>(dy)), 0);
    } else {
        /* ROT_0 快速路径：直接按字节批量填充 */
        if (g_oled_rotation == OLED_ROT_0) {
            const uint8_t xs = static_cast<uint8_t>(x < x1 ? x : x1);
            const uint8_t xe = static_cast<uint8_t>(x > x1 ? x : x1);
            const uint8_t ys = static_cast<uint8_t>(y < y1 ? y : y1);
            const uint8_t ye = static_cast<uint8_t>(y > y1 ? y : y1);
            OLED_Fill_Rect_Fast(xs, xe, ys, ye);
        } else {
            const int32_t step = (dy > 0) ? 1 : -1;
            const int32_t end = y1 + step;
            for (int32_t row = y; row != end; row += step) {
                OLED_Draw_Line(x, static_cast<int16_t>(row), dx, 0, 0);
            }
        }
    }
}

#if OLED_ENABLE_WAVE

/**
 * @brief  在 OLED_GRAM 中绘制三角函数波形（不刷新屏幕）
 * @param  x0:     坐标原点列坐标（屏幕像素坐标，整型可越界，到达真正的原点才有画面）
 * @param  y0:     坐标原点行坐标（屏幕像素坐标，整型可越界）
 * @param  A:      振幅（像素，uint8_t，0~31 为合理范围）
 * @param  wave:   0=sin，1=cos
 * @param  period: 周期长度（像素/周期，>0，如 64=每周期 64 列）
 * @param  phi:    初相位（SIN_LUT 索引单位，0~255，对应 0~2π）
 * @param  b:      垂直位移（像素，正=上移，负=下移，数学坐标系方向）
 * @note   纯整数运算，无 FPU 需求。内部调用 OLED_Draw_Point，不包含刷新操作。
 */
void OLED_Draw_Wave(int16_t x0, int16_t y0, uint8_t A, uint8_t wave,
                     uint16_t period, uint8_t phi, int16_t b)
{
    if (period == 0 || A == 0) return;

    // period → LUT 步长（Q8.8 定点，256 LUT 项 = 2π）
    uint16_t step = (uint16_t)(65536u / period);

    // cos(x) = sin(x + π/2)，在 LUT 中偏移 256/4 = 64
    if (wave) phi += 64;

    // 有效初相位 (Q8.8)：phi*256 - step*x0，uint16 自然回绕取模
    uint16_t phase = ((uint16_t)phi << 8) - (uint16_t)((int32_t)step * x0);

    for (uint16_t sx = 0; sx < OLED_WIDTH; sx++) {
        uint8_t idx = (uint8_t)(phase >> 8);        // LUT 索引 (0~255)
        int16_t sv  = SIN_LUT[idx];                  // Q1.15 有符号

        // 缩放至像素：y_math = sv * A / 32768
        int16_t y_math = (int16_t)(((int32_t)sv * A) >> 15);

        // 屏幕坐标：y_screen = y0 - (y_math + b)
        int16_t sy = y0 - y_math - b;

        if (sy >= 0 && sy < OLED_HEIGHT)
            OLED_Draw_Point((uint8_t)sx, (uint8_t)sy);

        phase += step;
    }
}

#endif /* OLED_ENABLE_WAVE */

/**
 * @brief  在 OLED_GRAM 中打印数字（整数或浮点数，不刷新屏幕）
 * @param  num:          数字的指针 (void*，按 type 解引用)
 * @param  type:         OLED_NUM_S8 ~ OLED_NUM_FLOAT
 * @param  bits_or_prec: 整型忽略；浮点型=小数位数(0~9)
 * @param  size:         字号 "0806"/"1206"/"1608"/"2412"
 * @param  x:            起始列坐标 (0~127)
 * @param  y:            起始行坐标 (0~63)
 * @note   栈上自建字符串，不用 sprintf/malloc 依赖。
 */
void OLED_Show_Number(const void* num, OLED_NumType type,
                      uint8_t bits_or_prec, const char* size,
                      uint8_t x, uint8_t y)
{
    if (num == NULL || size == NULL || x >= OLED_LOG_W || y >= OLED_LOG_H) return;

    char buf[16];          // -32768.xxxxx\0
    char* p = buf + 15;
    *p-- = '\0';

    if (type != OLED_NUM_FLOAT) {
        uint32_t v;
        int sign = 0;
        switch (type) {
            case OLED_NUM_S8:  { int8_t  t = *static_cast<const int8_t*>(num);  sign = t < 0; v = sign ? static_cast<uint32_t>(-static_cast<int32_t>(t)) : static_cast<uint32_t>(t); } break;
            case OLED_NUM_U8:    v = *static_cast<const uint8_t*>(num);  break;
            case OLED_NUM_S16: { int16_t t = *static_cast<const int16_t*>(num); sign = t < 0; v = sign ? static_cast<uint32_t>(-static_cast<int32_t>(t)) : static_cast<uint32_t>(t); } break;
            case OLED_NUM_U16:   v = *static_cast<const uint16_t*>(num); break;
            case OLED_NUM_S32: {
                int32_t t = *static_cast<const int32_t*>(num);
                sign = t < 0;
                v = sign ? (uint32_t)(-(t + 1)) + 1U : (uint32_t)t;
            } break;
            case OLED_NUM_U32:   v = *static_cast<const uint32_t*>(num); break;
            default: return;
        }
        do { *p-- = (char)('0' + (v % 10)); v /= 10; } while (v);
        if (sign) *p-- = '-';
    } else {
        float f = *static_cast<const float*>(num);
        int sign = f < 0.0f;
        if (sign) f = -f;
        uint8_t prec = bits_or_prec > 9 ? 9 : bits_or_prec;

        static const uint32_t pow10_lut[] = {
            1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000
        };
        uint32_t pow10 = pow10_lut[prec];

        if (f != f) {
            OLED_Show_String("NaN", size, x, y);
            return;
        }
        if (f > (float)UINT32_MAX / (float)pow10) {
            OLED_Show_String("OVF", size, x, y);
            return;
        }

        uint32_t scaled = (uint32_t)(f * (float)pow10 + 0.5f);
        uint32_t ip   = scaled / pow10;
        uint32_t frac = scaled % pow10;

        for (uint8_t i = 0; i < prec; i++) {
            *p-- = (char)('0' + (frac % 10));
            frac /= 10;
        }
        if (prec) *p-- = '.';

        do { *p-- = (char)('0' + (ip % 10)); ip /= 10; } while (ip);

        if (sign) *p-- = '-';
    }

    OLED_Show_String(p + 1, size, x, y);
}

static void OLED_Draw_Clipped_Point(int32_t x, int32_t y,
                                    int32_t width, int32_t height)
{
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    OLED_Draw_Point((uint8_t)x, (uint8_t)y);
}

static void OLED_Draw_Clipped_HLine(int32_t x1, int32_t x2, int32_t y,
                                    int32_t width, int32_t height)
{
    if (y < 0 || y >= height) return;
    if (x1 > x2) {
        const int32_t swap = x1;
        x1 = x2;
        x2 = swap;
    }
    if (x2 < 0 || x1 >= width) return;
    if (x1 < 0) x1 = 0;
    if (x2 >= width) x2 = width - 1;

    if (g_oled_rotation == OLED_ROT_0) {
        OLED_HLine_Fast((uint8_t)x1, (uint8_t)x2, (uint8_t)y);
        return;
    }
    for (int32_t x = x1; x <= x2; ++x) {
        OLED_Draw_Point((uint8_t)x, (uint8_t)y);
    }
}

void OLED_Draw_Circle(int16_t x0, int16_t y0, uint8_t r, uint8_t mode)
{
    const int32_t width = OLED_LOG_W;
    const int32_t height = OLED_LOG_H;

    if ((int32_t)x0 + r < 0 || (int32_t)x0 - r >= width ||
        (int32_t)y0 + r < 0 || (int32_t)y0 - r >= height) return;

    int32_t x = r;
    int32_t y = 0;
    int32_t err = 1 - (int32_t)r;

    if (mode == 0) {
        while (x >= y) {
            OLED_Draw_Clipped_Point((int32_t)x0 + x, (int32_t)y0 + y, width, height);
            OLED_Draw_Clipped_Point((int32_t)x0 + y, (int32_t)y0 + x, width, height);
            OLED_Draw_Clipped_Point((int32_t)x0 - y, (int32_t)y0 + x, width, height);
            OLED_Draw_Clipped_Point((int32_t)x0 - x, (int32_t)y0 + y, width, height);
            OLED_Draw_Clipped_Point((int32_t)x0 - x, (int32_t)y0 - y, width, height);
            OLED_Draw_Clipped_Point((int32_t)x0 - y, (int32_t)y0 - x, width, height);
            OLED_Draw_Clipped_Point((int32_t)x0 + y, (int32_t)y0 - x, width, height);
            OLED_Draw_Clipped_Point((int32_t)x0 + x, (int32_t)y0 - y, width, height);
            y++;
            if (err <= 0) { err += 2 * y + 1; }
            else          { x--; err += 2 * (y - x) + 1; }
        }
    } else {
        while (x >= y) {
            OLED_Draw_Clipped_HLine((int32_t)x0 - x, (int32_t)x0 + x,
                                    (int32_t)y0 + y, width, height);
            OLED_Draw_Clipped_HLine((int32_t)x0 - y, (int32_t)x0 + y,
                                    (int32_t)y0 + x, width, height);
            OLED_Draw_Clipped_HLine((int32_t)x0 - x, (int32_t)x0 + x,
                                    (int32_t)y0 - y, width, height);
            OLED_Draw_Clipped_HLine((int32_t)x0 - y, (int32_t)x0 + y,
                                    (int32_t)y0 - x, width, height);
            y++;
            if (err <= 0) { err += 2 * y + 1; }
            else          { x--; err += 2 * (y - x) + 1; }
        }
    }
}



//=====================局部刷新函数区域=====================

/**
 * @brief  在 OLED_GRAM 中局部擦除矩形区域（不刷新屏幕）
 * @param  x0: 矩形左上角列坐标 (signed)
 * @param  y0: 矩形左上角行坐标 (signed)
 * @param  dx: 矩形宽度，正右负左
 * @param  dy: 矩形高度，正下负上
 * @note   与该矩形区域有交集的屏幕部分全部清零。内部按字节直接操作，
 *         仅对首尾跨页的字节做掩码保护。不刷新屏幕。
 *         ⚠️ 本函数直接操作物理显存页/列，**不受 OLED_Set_Rotation 影响**。
 *            旋转模式下建议使用 OLED_GRAM_Clear() 全屏清除后重绘。
 */
void OLED_Clear_Rect(int16_t x0, int16_t y0, int16_t dx, int16_t dy)
{
    OLED_ClippedRect rect;
    if (!OLED_Clip_Physical_Rect(x0, y0, dx, dy, &rect)) return;

    const uint8_t pg_top = (uint8_t)(rect.top >> 3);
    const uint8_t pg_bot = (uint8_t)((rect.bottom - 1U) >> 3);
    const uint8_t xs = (uint8_t)rect.left;
    const uint16_t cols = rect.right - rect.left;

    if (pg_top == pg_bot) {
        uint8_t mask = (uint8_t)((0xFF << (rect.top & 0x07U)) &
                                 (0xFF >> (7U - ((rect.bottom - 1U) & 0x07U))));
        uint8_t* base = DRAW_BUFFER(pg_top) + xs;
        for (uint16_t c = 0; c < cols; c++)
            base[c] &= ~mask;
    } else {
        // 首页
        {
            uint8_t mask = (uint8_t)(0xFF << (rect.top & 0x07U));
            uint8_t* base = DRAW_BUFFER(pg_top) + xs;
            for (uint16_t c = 0; c < cols; c++)
                base[c] &= ~mask;
        }
        // 中间完整页
        for (uint8_t pg = pg_top + 1; pg < pg_bot; pg++) {
            memset(DRAW_BUFFER(pg) + xs, 0x00, cols);
        }
        // 末页
        {
            uint8_t mask = (uint8_t)(0xFF >> (7U - ((rect.bottom - 1U) & 0x07U)));
            uint8_t* base = DRAW_BUFFER(pg_bot) + xs;
            for (uint16_t c = 0; c < cols; c++)
                base[c] &= ~mask;
        }
    }
}

/**
 * @brief  局部刷新 OLED_GRAM 中的矩形区域到屏幕（DMA）
 * @param  x0: 矩形左上角列坐标 (signed)
 * @param  y0: 矩形左上角行坐标 (signed)
 * @param  dx: 矩形宽度（像素数），正右负左
 * @param  dy: 矩形高度（像素数），正下负上
 * @note   区间为 [x0, x0+dx) × [y0, y0+dy)，与 Clear_Rect 一致。
 *         ⚠️ 本函数基于物理页/列发送数据，**不受 OLED_Set_Rotation 影响**。
 *            旋转模式下建议使用 OLED_GRAM_Refresh() 或 OLED_Swap_Buffers() 全帧刷新。
 *         双缓冲：取当前绘图缓冲（draw_buffer）的内容，并同步写回 OLED_GRAM，
 *            使 OLED_GRAM 保持为屏幕镜像，可与全帧刷新混用而不会推出旧帧。
 */
void OLED_Refresh_Rect(int16_t x0, int16_t y0, int16_t dx, int16_t dy)
{
    static volatile uint8_t refresh_in_progress = 0;
    if (refresh_in_progress) return;
    refresh_in_progress = 1;

    OLED_ClippedRect rect;
    if (!OLED_Clip_Physical_Rect(x0, y0, dx, dy, &rect)) goto exit;
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
#if OLED_USE_DOUBLE_BUFFER
    if (draw_buffer != OLED_GRAM) (void)memcpy(OLED_GRAM, draw_buffer, sizeof(OLED_GRAM));
#endif
    OLED_Wait_DMA();
    OLED_GRAM_Refresh();
    goto exit;
#endif
    {
        uint8_t  pg_start  = (uint8_t)(rect.top >> 3);
        uint8_t  pg_end    = (uint8_t)((rect.bottom - 1U) >> 3);
        uint8_t  col_start = (uint8_t)rect.left;
        uint8_t  col_end   = (uint8_t)(rect.right - 1U);
        uint16_t cols      = (uint16_t)(col_end - col_start + 1);

        /* 命令缓冲必须是 static：端口层 DMA 异步读取该地址，而
         * OLED_Wait_DMA 超时会提前返回，此时栈帧已释放，DMA 会读到垃圾。
         * 同 OLED_Write_Byte 中 dma_buf 的说明。 */
        static uint8_t win[6];

        OLED_Wait_DMA();

        // 设置寻址窗口
        win[0] = 0x21;
        win[1] = (uint8_t)(OLED_COLUMN_OFFSET + col_start);
        win[2] = (uint8_t)(OLED_COLUMN_OFFSET + col_end);
        win[3] = 0x22; win[4] = pg_start;  win[5] = pg_end;
        (void)OLED_DMA_Send(CMD, win, sizeof(win));

        OLED_Wait_DMA();

        /* 逐页直发：水平寻址模式下 SSD1306 内部指针在窗口内自动前进并换页，
         * 每页单独一次传输即可，无需先打包整块（省掉 1KB 静态缓冲）。 */
        for (uint8_t p = pg_start; p <= pg_end; p++) {
#if OLED_USE_DOUBLE_BUFFER
            /* 双缓冲下把当前绘图缓冲的该行段同步进 OLED_GRAM，使 OLED_GRAM 始终
             * 是"屏幕内容的镜像"——否则后续整帧刷新会把未同步的旧帧推上屏。 */
            if (draw_buffer != OLED_GRAM)
                memcpy(&OLED_GRAM[p][col_start], &draw_buffer[p][col_start], cols);
#endif
            (void)OLED_DMA_Send(DATA, &OLED_GRAM[p][col_start], cols);
            OLED_Wait_DMA();
        }

        // 恢复全屏窗口
        win[0] = 0x21;
        win[1] = OLED_COLUMN_OFFSET;
        win[2] = (uint8_t)(OLED_COLUMN_OFFSET + OLED_WIDTH - 1);
        win[3] = 0x22; win[4] = 0; win[5] = (uint8_t)(OLED_PAGES - 1);
        (void)OLED_DMA_Send(CMD, win, sizeof(win));

        OLED_Wait_DMA();
    }

exit:
    refresh_in_progress = 0;
}

//===================== 格式化输出 =====================

/**
 * @brief  类 printf 格式化输出到 OLED 屏幕
 * @param  size: 字号 "0806"/"1206"/"1608"/"2412"
 * @param  x:    起始列坐标 (0~127)
 * @param  y:    起始行坐标 (0~63)
 * @param  fmt:  printf 格式字符串
 * @param  ...:  可变参数
 * @note   内部使用 vsnprintf，缓冲区 64 字节。支持 %d/%u/%x/%s/%c/%f 等。
 *         不刷新屏幕，调用后需 OLED_GRAM_Refresh() 或 OLED_Swap_Buffers()。
 * @example OLED_Printf("1608", 0, 0, "T:%d.%dC", temp/10, temp%10);
 */
void OLED_Printf(const char* size, uint8_t x, uint8_t y, const char* fmt, ...)
{
    if (size == NULL || fmt == NULL) return;
    char buf[OLED_PRINTF_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OLED_Show_String(buf, size, x, y);
}

//===================== 整数快速打印 =====================

/**
 * @brief  快速打印有符号整数（无浮点、无类型分派）
 * @param  num:  要显示的整数值
 * @param  size: 字号 "0806"/"1206"/"1608"/"2412"
 * @param  x, y: 起始坐标
 */
void OLED_Show_Int(int32_t num, const char* size, uint8_t x, uint8_t y)
{
    char buf[12];          // -2147483648\0 = 12 字节
    char* p = buf + 11;
    *p = '\0';

    uint32_t v;
    uint8_t neg = 0;
    if (num < 0) { neg = 1; v = (uint32_t)(-(num + 1)) + 1u; }
    else         { v = (uint32_t)num; }

    do { *--p = (char)('0' + (v % 10u)); v /= 10u; } while (v);
    if (neg) *--p = '-';

    OLED_Show_String(p, size, x, y);
}

/**
 * @brief  快速打印无符号整数
 */
void OLED_Show_Uint(uint32_t num, const char* size, uint8_t x, uint8_t y)
{
    char buf[11];          // 4294967295\0 = 11 字节
    char* p = buf + 10;
    *p = '\0';

    do { *--p = (char)('0' + (num % 10u)); num /= 10u; } while (num);

    OLED_Show_String(p, size, x, y);
}

/**
 * @brief  快速打印十六进制数
 * @param  num:    数值
 * @param  digits: 显示位数（1~8），高位补零。0=自动（不补零）
 * @param  size:   字号
 * @param  x, y:   坐标
 */
void OLED_Show_Hex(uint32_t num, uint8_t digits, const char* size, uint8_t x, uint8_t y)
{
    static const char hex_lut[] = "0123456789ABCDEF";
    char buf[11];          // "0x" + 8 hex + '\0' = 11
    char* p = buf + 10;
    *p = '\0';

    if (digits == 0) {
        // 自动位数
        if (num == 0) { *--p = '0'; }
        else { while (num) { *--p = hex_lut[num & 0x0Fu]; num >>= 4; } }
    } else {
        if (digits > 8) digits = 8;
        for (uint8_t i = 0; i < digits; i++) {
            *--p = hex_lut[num & 0x0Fu];
            num >>= 4;
        }
    }
    *--p = 'x';
    *--p = '0';

    OLED_Show_String(p, size, x, y);
}

//===================== UI 控件 =====================

/**
 * @brief  绘制进度条控件
 * @param  x, y:     左上角坐标
 * @param  w:        总宽度（像素，含边框）
 * @param  h:        总高度（像素，含边框，建议 >=5）
 * @param  percent:  进度百分比 0~100
 * @param  style:    0=实心填充, 1=斜线条纹填充
 * @note   直接操作 draw_buffer，按字节写入，性能远优于逐像素绘制。
 *         不刷新屏幕。
 */
void OLED_Draw_ProgressBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                           uint8_t percent, uint8_t style)
{
    if (w < 4 || h < 3) return;
    if ((uint16_t)x + w > OLED_LOG_W || (uint16_t)y + h > OLED_LOG_H) return;
    if (percent > 100) percent = 100;

    const uint8_t right = (uint8_t)((uint16_t)x + w - 1U);
    const uint8_t bottom = (uint8_t)((uint16_t)y + h - 1U);

    // 外边框（四条水平/垂直线段用快速路径）
    if (g_oled_rotation == OLED_ROT_0 &&
        x + w <= OLED_WIDTH && y + h <= OLED_HEIGHT) {
        OLED_HLine_Fast(x, right, y);
        OLED_HLine_Fast(x, right, bottom);
        for (uint8_t row = y; row < y + h; row++) {
            OLED_DRAW_POINT_FAST(x, row);
            OLED_DRAW_POINT_FAST(right, row);
        }
    } else {
        OLED_Draw_Line(x, y, w - 1, 0, 0);
        OLED_Draw_Line(x, bottom, (int16_t)w - 1, 0, 0);
        OLED_Draw_Line(x, y, 0, h - 1, 0);
        OLED_Draw_Line(right, y, 0, (int16_t)h - 1, 0);
    }

    uint8_t inner_w = w - 2;
    uint8_t inner_h = h - 2;
    uint8_t fill_w  = (uint8_t)((uint16_t)inner_w * percent / 100u);
    if (fill_w == 0) return;

    uint8_t x0 = x + 1;
    uint8_t y0_f = y + 1;
    uint8_t ye = (uint8_t)((uint16_t)y0_f + inner_h - 1U);

    if (g_oled_rotation == OLED_ROT_0 &&
        x0 + fill_w <= OLED_WIDTH && ye < OLED_HEIGHT) {
        if (style == 0) {
            OLED_Fill_Rect_Fast(x0, (uint8_t)((uint16_t)x0 + fill_w - 1U), y0_f, ye);
        } else {
            for (uint8_t dy = 0; dy < inner_h; dy++) {
                uint8_t row = y0_f + dy;
                uint8_t pg  = row >> 3;
                uint8_t bit = (uint8_t)(1u << (row & 0x07));
                uint8_t* base = DRAW_BUFFER(pg) + x0;
                for (uint8_t dx = 0; dx < fill_w; dx++) {
                    if (((dx + dy) & 3u) < 2u)
                        base[dx] |= bit;
                }
            }
        }
    } else {
        for (uint8_t dy = 0; dy < inner_h; dy++) {
            for (uint8_t dx = 0; dx < fill_w; dx++) {
                if (style == 0 || ((dx + dy) & 3u) < 2u)
                    OLED_Draw_Point(x0 + dx, y0_f + dy);
            }
        }
    }
}

//===================== 软件滚动（循环滚动） =====================

/**
 * @brief  软件垂直循环滚动（上下平移显存内容，滚出边缘的像素从对侧重新出现）
 * @param  offset: 滚动像素数，正值=向下滚动，负值=向上滚动
 * @note   操作 draw_buffer（跟随 Select_Buffer 选择），不刷新屏幕。
 *         每列按页与页内位移组合，滚出顶/底部的像素从对侧绕回。
 *         与硬件滚动不同，软件滚动后可继续正常绘图、不冲突。
 *         滚动基于物理显存方向，不受旋转设置影响。
 */
void OLED_Scroll_Soft_Vertical(int16_t offset)
{
    int16_t normalized = (int16_t)(offset % OLED_HEIGHT);
    if (normalized < 0) normalized = (int16_t)(normalized + OLED_HEIGHT);
    if (normalized == 0) return;

    uint8_t page_shift = (uint8_t)(normalized >> 3);
    uint8_t bit_shift = (uint8_t)(normalized & 7);
    uint8_t temp[OLED_PAGES];

    for (uint16_t col = 0; col < OLED_WIDTH; ++col) {
        for (uint8_t pg = 0; pg < OLED_PAGES; ++pg) temp[pg] = draw_buffer[pg][col];
        for (uint8_t dst = 0; dst < OLED_PAGES; ++dst) {
            uint8_t src = (uint8_t)((dst + OLED_PAGES - page_shift) % OLED_PAGES);
            uint8_t value = (uint8_t)(temp[src] << bit_shift);
            if (bit_shift != 0U) {
                uint8_t prev = (uint8_t)((src + OLED_PAGES - 1U) % OLED_PAGES);
                value |= (uint8_t)(temp[prev] >> (8U - bit_shift));
            }
            draw_buffer[dst][col] = value;
        }
    }
}

/**
 * @brief  软件水平循环滚动（左右平移显存内容，滚出边缘的像素从对侧重新出现）
 * @param  offset: 滚动像素数，正值=向右滚动，负值=向左滚动
 * @note   操作 draw_buffer，不刷新屏幕。
 *         逐页对列数据做环形旋转，效率极高（仅 memcpy 级别）。
 */
void OLED_Scroll_Soft_Horizontal(int16_t offset)
{
    int16_t normalized = (int16_t)(offset % OLED_WIDTH);
    if (normalized < 0) normalized = (int16_t)(normalized + OLED_WIDTH);
    if (normalized == 0) return;

    uint8_t n = (uint8_t)normalized;  // 向右偏移量
    uint8_t temp[OLED_WIDTH];

    for (uint8_t pg = 0; pg < OLED_PAGES; pg++) {
        uint8_t* row = draw_buffer[pg];
        memcpy(temp, row, OLED_WIDTH);
        // 右移 n 列：原尾部 (WIDTH-n)..127 → 新位置 0..n-1
        memcpy(row, temp + OLED_WIDTH - n, n);
        // 原头部 0..(WIDTH-n-1) → 新位置 n..127
        memcpy(row + n, temp, OLED_WIDTH - n);
    }
}

