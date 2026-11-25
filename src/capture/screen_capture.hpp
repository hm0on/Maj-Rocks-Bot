#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rocks {

// Область экрана в физических пикселях, отсчёт от левого верхнего угла основного монитора.
struct CaptureRegion {
    int x;
    int y;
    int width;
    int height;
};

struct ScreenFrame {
    int width;
    int height;
    // Где на экране лежит левый верхний угол кадра. Нужно для перевода координат в клики.
    int desktop_x;
    int desktop_y;
    // Байт в одной строке: width * 4.
    std::size_t stride;
    // Пиксели BGRA подряд, без отступов. Строки идут сверху вниз.
    std::vector<std::uint8_t> bgra;
};

// Снимает область экрана через GDI. При ошибке бросает std::runtime_error.
[[nodiscard]] ScreenFrame capture_screen(const CaptureRegion& region);

} // namespace rocks
