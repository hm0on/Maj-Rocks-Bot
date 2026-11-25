#include "capture/screen_capture.hpp"
#include "detection/detector.hpp"
#include "input/mouse_controller.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <thread>
#include <vector>

namespace {

// Область которая передается потом в модельку, там где камушки спавняться
constexpr rocks::CaptureRegion kGameArea{592, 264, 800, 448};

// Путь к модели относительно папки, из которой запускаем программу.
constexpr const char* kModelPath = "models/last.onnx";

// Минимальная уверенность модели (0..1). Рамки ниже порога не считаем камушками.
constexpr float kMinScore = 0.7f;

// Время после запуска, чтобы переключиться на окно игры и убрать консоль с игровой области.
constexpr std::chrono::seconds kStartDelay{3};
// Пауза между кликами по соседним камушкам.
constexpr std::chrono::milliseconds kDelayBetweenClicks{100};

// Сколько миллисекунд прошло с момента started.
double milliseconds_since(std::chrono::steady_clock::time_point started) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
        .count();
}

} // namespace

int main() {
    // Работаем в физических пикселях, иначе при масштабе Windows не 100% съедут захват и клики.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    try {
        // Модель грузим один раз, это самая долгая операция.
        rocks::Detector detector(kModelPath, kMinScore);

        std::cout << "Switch to the game, starting in " << kStartDelay.count() << " s...\n";
        std::this_thread::sleep_for(kStartDelay);

        auto started = std::chrono::steady_clock::now();
        const rocks::ScreenFrame frame = rocks::capture_screen(kGameArea);
        std::cout << "Captured " << frame.width << "x" << frame.height << " in "
                  << milliseconds_since(started) << " ms\n";

        started = std::chrono::steady_clock::now();
        const std::vector<rocks::Detection> gems = detector.detect(frame);
        std::cout << "Found " << gems.size() << " gems in " << milliseconds_since(started)
                  << " ms\n";

        // Кликаем по очереди в центр каждой рамки.
        for (const rocks::Detection& gem : gems) {
            const int center_x = static_cast<int>(std::lround((gem.left + gem.right) / 2.0f));
            const int center_y = static_cast<int>(std::lround((gem.top + gem.bottom) / 2.0f));
            std::cout << "  click (" << center_x << ", " << center_y << "), score " << gem.score
                      << '\n';
            rocks::click_at(center_x, center_y);
            std::this_thread::sleep_for(kDelayBetweenClicks);
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
