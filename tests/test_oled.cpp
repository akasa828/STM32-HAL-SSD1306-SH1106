#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "oled.hpp"

namespace {

struct Transfer {
    uint8_t address = 0;
    uint8_t control = 0;
    std::vector<uint8_t> bytes;
};

struct FakePort {
    uint32_t tick = 0;
    int write_result = OLED_PORT_OK;
    bool complete_immediately = true;
    uint32_t aborts = 0;
    uint32_t recoveries = 0;
    uint32_t successes = 0;
    uint32_t failures = 0;
    uint32_t timeout_failures = 0;
    std::vector<Transfer> transfers;
};

int fake_write(void *context, uint8_t address, uint8_t control,
               uint8_t *data, uint16_t size)
{
    auto &port = *static_cast<FakePort *>(context);
    port.transfers.push_back({address, control,
                              std::vector<uint8_t>(data, data + size)});
    if (port.write_result == OLED_PORT_OK && port.complete_immediately) {
        OLED_NotifyTxComplete();
    }
    return port.write_result;
}

int fake_abort(void *context)
{
    ++static_cast<FakePort *>(context)->aborts;
    return OLED_PORT_OK;
}

int fake_recover(void *context)
{
    ++static_cast<FakePort *>(context)->recoveries;
    return OLED_PORT_OK;
}

uint32_t fake_tick(void *context)
{
    return static_cast<FakePort *>(context)->tick;
}

void fake_idle(void *context)
{
    ++static_cast<FakePort *>(context)->tick;
}

void fake_failure(void *context, uint8_t timeout_failure)
{
    auto &port = *static_cast<FakePort *>(context);
    ++port.failures;
    port.timeout_failures += timeout_failure != 0U;
}

void fake_success(void *context)
{
    ++static_cast<FakePort *>(context)->successes;
}

OLED_PortOps make_ops(FakePort &port)
{
    OLED_PortOps ops{};
    ops.context = &port;
    ops.write_dma = fake_write;
    ops.abort_dma = fake_abort;
    ops.recover = fake_recover;
    ops.tick_ms = fake_tick;
    ops.idle = fake_idle;
    ops.on_success = fake_success;
    ops.on_failure = fake_failure;
    return ops;
}

void reset_driver(FakePort &port)
{
    port = FakePort{};
    OLED_PortOps ops = make_ops(port);
    if (OLED_BindPort(&ops) != OLED_PORT_OK) {
        std::cerr << "OLED_BindPort failed\n";
        std::exit(EXIT_FAILURE);
    }
    std::memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    std::memset(OLED_BACK_BUFFER, 0, sizeof(OLED_BACK_BUFFER));
    OLED_Select_Buffer(0);
    OLED_Set_Rotation(OLED_ROT_0);
    port.transfers.clear();
}

bool all_bytes_equal(const uint8_t *data, std::size_t size, uint8_t value)
{
    return std::all_of(data, data + size,
                       [value](uint8_t byte) { return byte == value; });
}

bool pixel_is_set(const uint8_t frame[OLED_PAGES][OLED_WIDTH], int x, int y)
{
    return (frame[y >> 3][x] & (1U << (y & 7))) != 0U;
}

void set_pixel(uint8_t frame[OLED_PAGES][OLED_WIDTH], int x, int y, bool value)
{
    const uint8_t mask = static_cast<uint8_t>(1U << (y & 7));
    if (value) {
        frame[y >> 3][x] |= mask;
    } else {
        frame[y >> 3][x] &= static_cast<uint8_t>(~mask);
    }
}

struct TestContext {
    int failures = 0;

    void expect(bool condition, const std::string &message)
    {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void test_clear_presents_selected_buffer(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    std::memset(OLED_GRAM, 0xA5, sizeof(OLED_GRAM));
    std::memset(OLED_BACK_BUFFER, 0x5A, sizeof(OLED_BACK_BUFFER));
    OLED_Select_Buffer(1);

    OLED_Clear();
    OLED_Wait_DMA();

    test.expect(all_bytes_equal(&OLED_GRAM[0][0], OLED_GRAM_SIZE, 0),
                "OLED_Clear must clear the visible front buffer");
    test.expect(!port.transfers.empty(), "OLED_Clear must send a frame");
    if (!port.transfers.empty()) {
        const Transfer &frame = port.transfers.back();
        test.expect(frame.control == DATA, "OLED_Clear must finish with display data");
        test.expect(frame.bytes.size() == OLED_GRAM_SIZE,
                    "OLED_Clear must send the complete frame");
        test.expect(std::all_of(frame.bytes.begin(), frame.bytes.end(),
                                [](uint8_t byte) { return byte == 0; }),
                    "OLED_Clear must send cleared bytes");
    }
}

void test_swap_always_presents_background_buffer(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    std::memset(OLED_GRAM, 0xA5, sizeof(OLED_GRAM));
    std::memset(OLED_BACK_BUFFER, 0x5A, sizeof(OLED_BACK_BUFFER));
    OLED_Select_Buffer(0);

    OLED_Swap_Buffers();
    OLED_Wait_DMA();

    test.expect(all_bytes_equal(&OLED_GRAM[0][0], OLED_GRAM_SIZE, 0x5A),
                "OLED_Swap_Buffers must present the background buffer");
    test.expect(g_current_buffer_id == 1 && draw_buffer == OLED_BACK_BUFFER,
                "OLED_Swap_Buffers must leave drawing on the background buffer");
}

void test_partial_refresh_uses_column_offset(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    OLED_GRAM[0][1] = 0x11;
    OLED_GRAM[0][2] = 0x22;

    OLED_Refresh_Rect(1, 0, 2, 8);

    test.expect(port.transfers.size() >= 3,
                "partial refresh must send a window, data, and restore command");
    if (port.transfers.size() >= 3) {
        const std::vector<uint8_t> expected_window{
            0x21, static_cast<uint8_t>(OLED_COLUMN_OFFSET + 1),
            static_cast<uint8_t>(OLED_COLUMN_OFFSET + 2), 0x22, 0x00, 0x00};
        test.expect(port.transfers[0].bytes == expected_window,
                    "partial refresh window must include OLED_COLUMN_OFFSET");

        const std::vector<uint8_t> expected_restore{
            0x21, OLED_COLUMN_OFFSET,
            static_cast<uint8_t>(OLED_COLUMN_OFFSET + OLED_WIDTH - 1),
            0x22, 0x00, static_cast<uint8_t>(OLED_PAGES - 1)};
        test.expect(port.transfers.back().bytes == expected_restore,
                    "restored full window must include OLED_COLUMN_OFFSET");
    }
}

void test_offscreen_progress_bar_does_not_wrap(TestContext &test)
{
    FakePort port;
    reset_driver(port);

    OLED_Draw_ProgressBar(255, 0, 4, 4, 100, 0);

    test.expect(all_bytes_equal(&OLED_GRAM[0][0], OLED_GRAM_SIZE, 0),
                "an offscreen progress bar must not wrap to the left edge");
}

void test_immediate_timeout_is_counted(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    const uint32_t before = OLED_Get_I2C_Timeout_Count();
    port.write_result = OLED_PORT_TIMEOUT;

    OLED_Write_Byte(0xAE, CMD);

    test.expect(OLED_Get_I2C_Timeout_Count() == before + 1,
                "an immediate port timeout must increment the timeout count");
    test.expect(port.timeout_failures == 1,
                "an immediate port timeout must be reported as a timeout failure");
    test.expect(port.aborts == 1 && port.recoveries == 1,
                "an immediate port timeout must recover the transport");
}

void test_timeout_wait_recovers(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    const uint32_t errors_before = OLED_Get_I2C_Error_Count();
    const uint32_t before = OLED_Get_I2C_Timeout_Count();
    port.complete_immediately = false;

    OLED_Write_Byte(0xAE, CMD);
    OLED_Wait_DMA();

    test.expect(OLED_DMA_Busy == 0, "DMA timeout must clear the busy flag");
    test.expect(OLED_Get_I2C_Timeout_Count() == before + 1,
                "DMA wait timeout must increment the timeout count");
    test.expect(OLED_Get_I2C_Error_Count() == errors_before + 1,
                "DMA wait timeout must increment the transfer error count");
    test.expect(port.timeout_failures == 1,
                "DMA wait timeout must be reported as a timeout failure");
    test.expect(port.aborts == 1 && port.recoveries == 1,
                "DMA wait timeout must abort and recover the transport");
}

void test_notifications_require_an_active_transfer(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    const uint32_t errors_before = OLED_Get_I2C_Error_Count();

    OLED_NotifyTxComplete();
    OLED_NotifyError();

    test.expect(port.successes == 0U,
                "an idle completion notification must be ignored");
    test.expect(port.failures == 0U &&
                OLED_Get_I2C_Error_Count() == errors_before,
                "an idle error notification must be ignored");

    port.complete_immediately = false;
    OLED_Write_Byte(0xAE, CMD);
    OLED_NotifyTxComplete();
    OLED_NotifyTxComplete();
    test.expect(port.successes == 1U,
                "duplicate completion notifications must be ignored");
}

void test_rebinding_during_transfer_is_rejected(TestContext &test)
{
    FakePort active_port;
    reset_driver(active_port);
    active_port.complete_immediately = false;
    OLED_Write_Byte(0xAE, CMD);

    FakePort replacement;
    OLED_PortOps replacement_ops = make_ops(replacement);
    test.expect(OLED_BindPort(&replacement_ops) == OLED_PORT_BUSY,
                "port rebinding must not discard an active DMA transfer");

    OLED_NotifyTxComplete();
    test.expect(active_port.successes == 1U,
                "the active port must receive its completion notification");
    test.expect(OLED_BindPort(&replacement_ops) == OLED_PORT_OK,
                "port rebinding must succeed after the transfer completes");
}

void test_null_text_arguments_are_ignored(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    OLED_Show_Char_ASCII('A', nullptr, 0, 0);
    OLED_Show_String("A", nullptr, 0, 0);
    OLED_Printf(nullptr, 0, 0, "%s", "A");

    test.expect(all_bytes_equal(&OLED_GRAM[0][0], OLED_GRAM_SIZE, 0),
                "null text format arguments must leave the frame unchanged");
}

void test_minimum_signed_number(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    const int32_t value = INT32_MIN;
    OLED_Show_Number(&value, OLED_NUM_S32, 0, "0806", 0, 0);
    test.expect(!all_bytes_equal(&OLED_GRAM[0][0], OLED_GRAM_SIZE, 0),
                "INT32_MIN must render without signed overflow");
}

void test_bitmap_clipping_and_alignment(TestContext &test)
{
    constexpr uint8_t bitmap_width = 5;
    constexpr uint8_t bitmap_height = 11;
    const uint8_t bitmap[] = {
        0xA5, 0x5A, 0x81, 0x7E, 0x18,
        0x05, 0x02, 0x07, 0x00, 0x06,
    };
    const int positions[] = {-7, -1, 0, 1, 7, 14, 16};

    for (int y : positions) {
        for (int x : positions) {
            FakePort port;
            reset_driver(port);
            std::memset(OLED_GRAM, 0x96, sizeof(OLED_GRAM));
            uint8_t expected[OLED_PAGES][OLED_WIDTH];
            std::memcpy(expected, OLED_GRAM, sizeof(expected));

            for (int by = 0; by < bitmap_height; ++by) {
                for (int bx = 0; bx < bitmap_width; ++bx) {
                    const int screen_x = x + bx;
                    const int screen_y = y + by;
                    if (screen_x < 0 || screen_x >= OLED_WIDTH ||
                        screen_y < 0 || screen_y >= OLED_HEIGHT) {
                        continue;
                    }
                    const bool value =
                        (bitmap[(by >> 3) * bitmap_width + bx] &
                         (1U << (by & 7))) != 0U;
                    set_pixel(expected, screen_x, screen_y, value);
                }
            }

            OLED_Draw_Bitmap(static_cast<int16_t>(x), static_cast<int16_t>(y),
                             bitmap_width, bitmap_height, bitmap);
            test.expect(std::memcmp(OLED_GRAM, expected, sizeof(expected)) == 0,
                        "bitmap drawing must match clipped page-major pixels at " +
                            std::to_string(x) + "," + std::to_string(y));
        }
    }
}

void test_soft_scroll_matches_pixel_rotation(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    for (int y = 0; y < OLED_HEIGHT; ++y) {
        for (int x = 0; x < OLED_WIDTH; ++x) {
            set_pixel(OLED_GRAM, x, y, ((x * 3 + y * 5) % 7) < 3);
        }
    }
    uint8_t source[OLED_PAGES][OLED_WIDTH];
    std::memcpy(source, OLED_GRAM, sizeof(source));

    OLED_Scroll_Soft_Vertical(-5);
    for (int y = 0; y < OLED_HEIGHT; ++y) {
        for (int x = 0; x < OLED_WIDTH; ++x) {
            const int source_y = (y + 5) % OLED_HEIGHT;
            test.expect(pixel_is_set(OLED_GRAM, x, y) ==
                            pixel_is_set(source, x, source_y),
                        "vertical scrolling must rotate pixels exactly");
        }
    }

    std::memcpy(OLED_GRAM, source, sizeof(source));
    OLED_Scroll_Soft_Horizontal(3);
    for (int y = 0; y < OLED_HEIGHT; ++y) {
        for (int x = 0; x < OLED_WIDTH; ++x) {
            const int source_x = (x + OLED_WIDTH - 3) % OLED_WIDTH;
            test.expect(pixel_is_set(OLED_GRAM, x, y) ==
                            pixel_is_set(source, source_x, y),
                        "horizontal scrolling must rotate pixels exactly");
        }
    }
}

void test_infinite_lines_are_clipped_from_offscreen_origins(TestContext &test)
{
    FakePort port;
    reset_driver(port);
    OLED_Draw_Line(-20, 3, 1, 0, 1);
    for (int y = 0; y < OLED_HEIGHT; ++y) {
        for (int x = 0; x < OLED_WIDTH; ++x) {
            test.expect(pixel_is_set(OLED_GRAM, x, y) == (y == 3),
                        "horizontal infinite line must cross the full display");
        }
    }

    std::memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    OLED_Draw_Line(4, -20, 0, 1, 1);
    for (int y = 0; y < OLED_HEIGHT; ++y) {
        for (int x = 0; x < OLED_WIDTH; ++x) {
            test.expect(pixel_is_set(OLED_GRAM, x, y) == (x == 4),
                        "vertical infinite line must cross the full display");
        }
    }

    std::memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    OLED_Draw_Line(-5, -5, 1, 1, 1);
    for (int i = 0; i < OLED_WIDTH; ++i) {
        test.expect(pixel_is_set(OLED_GRAM, i, i),
                    "diagonal infinite line must be clipped onto the display");
    }

    std::memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    OLED_Draw_Line(0, 8, INT16_MIN, 1, 1);
    test.expect(pixel_is_set(OLED_GRAM, 0, 8),
                "extreme line deltas must not overflow or hang");
}

void test_rectangle_operations_match_half_open_clipping(TestContext &test)
{
    struct RectCase { int16_t x; int16_t y; int16_t w; int16_t h; };
    const RectCase cases[] = {
        {2, 3, 5, 7}, {-3, -2, 8, 6}, {10, 12, 20, 20},
        {8, 8, -6, -5}, {0, 0, 0, 5}, {0, 0, 5, 0},
        {0, 0, INT16_MIN, INT16_MIN},
    };

    for (const RectCase &item : cases) {
        FakePort port;
        reset_driver(port);
        std::memset(OLED_GRAM, 0xFF, sizeof(OLED_GRAM));
        uint8_t expected[OLED_PAGES][OLED_WIDTH];
        std::memcpy(expected, OLED_GRAM, sizeof(expected));

        const int32_t x_end = (int32_t)item.x + item.w;
        const int32_t y_end = (int32_t)item.y + item.h;
        const int32_t left = std::max<int32_t>(0, std::min<int32_t>(item.x, x_end));
        const int32_t right = std::min<int32_t>(OLED_WIDTH, std::max<int32_t>(item.x, x_end));
        const int32_t top = std::max<int32_t>(0, std::min<int32_t>(item.y, y_end));
        const int32_t bottom = std::min<int32_t>(OLED_HEIGHT, std::max<int32_t>(item.y, y_end));
        if (item.w != 0 && item.h != 0 && left < right && top < bottom) {
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) set_pixel(expected, x, y, false);
            }
        }

        OLED_Clear_Rect(item.x, item.y, item.w, item.h);
        test.expect(std::memcmp(OLED_GRAM, expected, sizeof(expected)) == 0,
                    "clear rectangle must use clipped half-open bounds");
    }
}

void apply_reference_rectangle(uint8_t frame[OLED_PAGES][OLED_WIDTH],
                               int16_t x, int16_t y, int16_t width,
                               int16_t height, bool invert)
{
    if (width == 0 || height == 0) return;
    const int32_t x_end = static_cast<int32_t>(x) + width;
    const int32_t y_end = static_cast<int32_t>(y) + height;
    const int32_t left = std::max<int32_t>(0, std::min<int32_t>(x, x_end));
    const int32_t right = std::min<int32_t>(OLED_WIDTH,
                                           std::max<int32_t>(x, x_end));
    const int32_t top = std::max<int32_t>(0, std::min<int32_t>(y, y_end));
    const int32_t bottom = std::min<int32_t>(OLED_HEIGHT,
                                            std::max<int32_t>(y, y_end));
    for (int32_t row = top; row < bottom; ++row) {
        for (int32_t column = left; column < right; ++column) {
            const bool before = pixel_is_set(frame, column, row);
            set_pixel(frame, column, row, invert ? !before : false);
        }
    }
}

void test_rectangle_property_cases(TestContext &test)
{
    constexpr int16_t origins[] = {-20, -16, -9, -1, 0, 1, 7, 15, 16, 17, 25};
    constexpr int16_t spans[] = {-20, -16, -7, -1, 0, 1, 7, 16, 17, 25};
    FakePort port;
    for (int16_t x : origins) {
        for (int16_t y : origins) {
            for (int16_t width : spans) {
                for (int16_t height : spans) {
                    reset_driver(port);
                    for (std::size_t page = 0; page < OLED_PAGES; ++page) {
                        for (std::size_t column = 0; column < OLED_WIDTH; ++column) {
                            const std::size_t index = page * OLED_WIDTH + column;
                            OLED_GRAM[page][column] =
                                static_cast<uint8_t>(index * 73U + 19U);
                        }
                    }
                    uint8_t expected[OLED_PAGES][OLED_WIDTH];
                    std::memcpy(expected, OLED_GRAM, sizeof(expected));
                    apply_reference_rectangle(expected, x, y, width, height, true);
                    OLED_SW_Invert_Rect(x, y, width, height);
                    test.expect(std::memcmp(OLED_GRAM, expected, sizeof(expected)) == 0,
                                "invert rectangle property case");

                    apply_reference_rectangle(expected, x, y, width, height, false);
                    OLED_Clear_Rect(x, y, width, height);
                    test.expect(std::memcmp(OLED_GRAM, expected, sizeof(expected)) == 0,
                                "clear rectangle property case");
                }
            }
        }
    }
}

void test_out_of_range_rectangle_does_not_overflow(TestContext &test)
{
    FakePort port;
    reset_driver(port);

    OLED_Draw_Rectang(1, 1, INT16_MAX, INT16_MAX, 1);
    OLED_Draw_Rectang(1, 1, INT16_MIN, INT16_MIN, 0);

    test.expect(all_bytes_equal(&OLED_GRAM[0][0], OLED_GRAM_SIZE, 0),
                "out-of-range rectangle endpoints must not overflow or draw");
}

void test_wave_coordinates_do_not_wrap(TestContext &test)
{
    FakePort port;
    reset_driver(port);

    OLED_Draw_Wave(0, INT16_MIN, 255U, 0U, 64U, 0U, INT16_MAX);

    test.expect(all_bytes_equal(&OLED_GRAM[0][0], OLED_GRAM_SIZE, 0),
                "offscreen wave coordinates must not wrap onto the display");
}

}

int main()
{
    TestContext test;
    test_clear_presents_selected_buffer(test);
    test_swap_always_presents_background_buffer(test);
    test_partial_refresh_uses_column_offset(test);
    test_offscreen_progress_bar_does_not_wrap(test);
    test_immediate_timeout_is_counted(test);
    test_timeout_wait_recovers(test);
    test_notifications_require_an_active_transfer(test);
    test_rebinding_during_transfer_is_rejected(test);
    test_null_text_arguments_are_ignored(test);
    test_minimum_signed_number(test);
    test_bitmap_clipping_and_alignment(test);
    test_soft_scroll_matches_pixel_rotation(test);
    test_infinite_lines_are_clipped_from_offscreen_origins(test);
    test_rectangle_operations_match_half_open_clipping(test);
    test_rectangle_property_cases(test);
    test_out_of_range_rectangle_does_not_overflow(test);
    test_wave_coordinates_do_not_wrap(test);

    if (test.failures != 0) {
        std::cerr << test.failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All OLED host tests passed\n";
    return 0;
}
