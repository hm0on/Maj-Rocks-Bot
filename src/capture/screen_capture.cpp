#include "screen_capture.hpp"

// Убираем макросы min/max из windows.h, иначе ломаются std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Отключаем редко нужные части windows.h, GDI остаётся.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdexcept>

namespace rocks {

ScreenFrame capture_screen(const CaptureRegion& region) {
    if (region.width <= 0 || region.height <= 0) {
        throw std::invalid_argument("Capture region must have a positive size.");
    }

    // Контекст всего экрана, с него копируем.
    HDC screen = GetDC(nullptr);
    if (!screen) {
        throw std::runtime_error("GetDC failed.");
    }
    // Холст в памяти, в него копируем.
    HDC memory = CreateCompatibleDC(screen);

    // Описание битмапа: 32 бита на пиксель (BGRA), без сжатия.
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = region.width;
    info.bmiHeader.biHeight = -region.height; // Минус: строки идут сверху вниз.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    // Битмап с прямым доступом к пикселям через bits.
    void* bits = nullptr;
    HBITMAP bitmap =
        memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    // Вставляем битмап в холст; прежний объект запоминаем, чтобы вернуть перед удалением.
    HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : nullptr;
    // Копируем прямоугольник с экрана в битмап.
    const bool copied = previous && BitBlt(memory, 0, 0, region.width, region.height,
                                           screen, region.x, region.y, SRCCOPY);

    ScreenFrame frame{region.width, region.height, region.x, region.y,
                      static_cast<std::size_t>(region.width) * 4, {}};
    if (copied) {
        // Дожидаемся, пока GDI допишет пиксели, и только потом читаем.
        GdiFlush();
        // Копируем пиксели в кадр: битмап сейчас будет удалён.
        const auto* pixels = static_cast<const std::uint8_t*>(bits);
        frame.bgra.assign(pixels, pixels + frame.stride * static_cast<std::size_t>(region.height));
    }

    // Освобождаем ресурсы GDI в обратном порядке. Любой шаг мог не выполниться, поэтому проверяем.
    if (previous) {
        SelectObject(memory, previous);
    }
    if (bitmap) {
        DeleteObject(bitmap);
    }
    if (memory) {
        DeleteDC(memory);
    }
    ReleaseDC(nullptr, screen);

    // Ошибку бросаем только после уборки, чтобы ничего не утекло.
    if (!copied) {
        throw std::runtime_error("Screen capture failed.");
    }
    return frame;
}

} // namespace rocks
