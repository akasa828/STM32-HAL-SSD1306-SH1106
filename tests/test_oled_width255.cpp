#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "oled.hpp"

namespace {

int write_dma(void *, uint8_t, uint8_t, uint8_t *, uint16_t)
{
    OLED_NotifyTxComplete();
    return OLED_PORT_OK;
}

uint32_t tick_ms(void *)
{
    return 0U;
}

bool expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

}

int main()
{
    OLED_PortOps ops{};
    ops.write_dma = write_dma;
    ops.tick_ms = tick_ms;
    if (!expect(OLED_BindPort(&ops) == OLED_PORT_OK, "port binding")) return 1;

    std::array<uint8_t, OLED_WIDTH> bitmap{};
    bitmap.fill(0xFFU);
    std::memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    OLED_Draw_Bitmap(0, 0, OLED_WIDTH, OLED_HEIGHT, bitmap.data());
    if (!expect(std::all_of(&OLED_GRAM[0][0], &OLED_GRAM[0][0] + OLED_WIDTH,
                            [](uint8_t byte) { return byte == 0xFFU; }),
                "255-column bitmap drawing")) return 1;

    OLED_Clear_Rect(0, 0, OLED_WIDTH, OLED_HEIGHT);
    if (!expect(std::all_of(&OLED_GRAM[0][0], &OLED_GRAM[0][0] + OLED_WIDTH,
                            [](uint8_t byte) { return byte == 0U; }),
                "255-column rectangle clearing")) return 1;

    std::cout << "OLED 255-column test passed\n";
    return 0;
}
