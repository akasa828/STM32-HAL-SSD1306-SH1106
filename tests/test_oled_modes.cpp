#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "oled.hpp"

namespace {

struct Transfer {
    uint8_t control;
    std::vector<uint8_t> bytes;
};

struct FakePort {
    uint32_t tick = 0;
    std::vector<Transfer> transfers;
};

int write_dma(void *context, uint8_t, uint8_t control,
              uint8_t *data, uint16_t size)
{
    auto &port = *static_cast<FakePort *>(context);
    port.transfers.push_back({control, std::vector<uint8_t>(data, data + size)});
    OLED_NotifyTxComplete();
    return OLED_PORT_OK;
}

uint32_t tick_ms(void *context)
{
    return static_cast<FakePort *>(context)->tick;
}

bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool command_was_sent(const FakePort &port, uint8_t command)
{
    for (const Transfer &transfer : port.transfers) {
        if (transfer.control == CMD &&
            std::find(transfer.bytes.begin(), transfer.bytes.end(), command)
                != transfer.bytes.end()) {
            return true;
        }
    }
    return false;
}

}

int main()
{
    FakePort port;
    OLED_PortOps ops{};
    ops.context = &port;
    ops.write_dma = write_dma;
    ops.tick_ms = tick_ms;
    if (!expect(OLED_BindPort(&ops) == OLED_PORT_OK, "port binding")) return 1;

    OLED_Init();
    OLED_Wait_DMA();
#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    if (!expect(!command_was_sent(port, 0x2EU) &&
                !command_was_sent(port, 0xA3U) &&
                !command_was_sent(port, 0x20U),
                "SH1106 initialization must omit SSD1306-only commands")) return 1;
    port.transfers.clear();
    OLED_Scroll_HW_H(0U, 0U, 0U, 0U);
    OLED_Scroll_HW_HV(0U, 0U, 0U, 0U, 1U);
    OLED_Scroll_HW_Switch(1U);
    if (!expect(port.transfers.empty(),
                "SH1106 must not receive SSD1306 hardware scroll commands")) return 1;
#else
    if (!expect(command_was_sent(port, 0x2EU) &&
                command_was_sent(port, 0xA3U) &&
                command_was_sent(port, 0x20U),
                "SSD1306 initialization must configure scrolling and addressing")) return 1;
#endif
    port.transfers.clear();

    std::memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
#if OLED_USE_DOUBLE_BUFFER
    std::memset(OLED_BACK_BUFFER, 0, sizeof(OLED_BACK_BUFFER));
    OLED_Select_Buffer(1);
#endif
    OLED_Draw_Point(12, 15);
    OLED_Swap_Buffers();
    OLED_Wait_DMA();

    if (!expect((OLED_GRAM[1][12] & 0x80U) != 0U,
                "non-power-of-two framebuffer drawing")) return 1;

#if OLED_CONTROLLER == OLED_CONTROLLER_SH1106
    if (!expect(port.transfers.size() == OLED_PAGES * 2U,
                "SH1106 refresh transfer count")) return 1;
    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        const Transfer &command = port.transfers[page * 2U];
        const std::vector<uint8_t> expected{
            static_cast<uint8_t>(0xB0U | page),
            static_cast<uint8_t>(OLED_COLUMN_OFFSET & 0x0FU),
            static_cast<uint8_t>(0x10U | ((OLED_COLUMN_OFFSET >> 4U) & 0x0FU)),
        };
        if (!expect(command.control == CMD && command.bytes == expected,
                    "SH1106 page addressing command")) return 1;
        if (!expect(port.transfers[page * 2U + 1U].bytes.size() == OLED_WIDTH,
                    "SH1106 page data size")) return 1;
    }
#else
    if (!expect(port.transfers.size() == 1U, "SSD1306 full refresh transfer count")) return 1;
    if (!expect(port.transfers.front().control == DATA &&
                port.transfers.front().bytes.size() == OLED_GRAM_SIZE,
                "SSD1306 full refresh data")) return 1;
#endif

    std::cout << "OLED mode test passed\n";
    return 0;
}
