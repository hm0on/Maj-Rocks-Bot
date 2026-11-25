#include "mouse_controller.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace rocks {
namespace {

// Пауза после перемещения курсора: игра должна успеть заметить, что курсор над камушком.
constexpr std::chrono::milliseconds kHoverDelay{20};
// Сколько держим кнопку нажатой. Слишком короткий клик некоторые игры не засчитывают.
constexpr std::chrono::milliseconds kPressDuration{30};

// Отправляет одно событие левой кнопки мыши (нажатие или отпускание).
void send_mouse_button(DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        throw std::runtime_error("SendInput failed.");
    }
}

} // namespace

void click_at(int x, int y) {
    if (!SetCursorPos(x, y)) {
        throw std::runtime_error("SetCursorPos failed.");
    }
    std::this_thread::sleep_for(kHoverDelay);

    send_mouse_button(MOUSEEVENTF_LEFTDOWN);
    std::this_thread::sleep_for(kPressDuration);
    send_mouse_button(MOUSEEVENTF_LEFTUP);
}

} // namespace rocks
