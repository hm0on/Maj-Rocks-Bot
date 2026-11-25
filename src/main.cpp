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
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

// Область которая передается потом в модельку, там где камушки спавняться
constexpr rocks::CaptureRegion kGameArea{592, 264, 800, 448};

// Путь к модели относительно папки, из которой запускаем программу.
constexpr const char* kModelPath = "models/last.onnx";

// Файл с кодом клавиши включения/выключения. Удали его, чтобы забиндить другую клавишу.
constexpr const char* kToggleKeyPath = "toggle_key.txt";

// Минимальная уверенность модели (0..1). Рамки ниже порога не считаем камушками.
constexpr float kMinScore = 0.7f;

// Пауза между кликами по соседним камушкам.
constexpr std::chrono::milliseconds kDelayBetweenClicks{100};
// Пауза после прохода по камушкам, чтобы игра успела убрать нажатые и не кликать по ним повторно.
constexpr std::chrono::milliseconds kDelayBetweenScans{300};
// Как часто проверяем клавиатуру, пока ждём нажатия.
constexpr std::chrono::milliseconds kKeyPollInterval{20};

// Зажата ли клавиша прямо сейчас. Работает, даже когда активно окно игры, а не консоль.
bool is_key_down(int key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

// Первая зажатая клавиша клавиатуры или 0. Коды 0x01–0x07 (кнопки мыши) пропускаем:
// программа сама кликает левой кнопкой и иначе выключала бы себя своими же кликами.
int find_pressed_key() {
    for (int key = 0x08; key <= 0xFE; ++key) {
        if (is_key_down(key)) {
            return key;
        }
    }
    return 0;
}

// Ждёт, пока пользователь нажмёт и отпустит любую клавишу, и возвращает её код.
int wait_for_key_press() {
    // Сначала ждём, пока отпустят всё (например, Enter, которым запустили программу).
    while (find_pressed_key() != 0) {
        std::this_thread::sleep_for(kKeyPollInterval);
    }
    int key = 0;
    while ((key = find_pressed_key()) == 0) {
        std::this_thread::sleep_for(kKeyPollInterval);
    }
    while (is_key_down(key)) {
        std::this_thread::sleep_for(kKeyPollInterval);
    }
    return key;
}

// Сохранённая клавиша, если файл есть и в нём корректный код.
std::optional<int> load_toggle_key() {
    std::ifstream file(kToggleKeyPath);
    int key = 0;
    if (file >> key && key >= 0x08 && key <= 0xFE) {
        return key;
    }
    return std::nullopt;
}

void save_toggle_key(int key) {
    std::ofstream file(kToggleKeyPath);
    file << key << '\n';
    if (!file) {
        throw std::runtime_error("Cannot save the toggle key.");
    }
}

// Берёт клавишу из файла, а при первом запуске просит нажать её и сохраняет.
int get_toggle_key() {
    if (const std::optional<int> saved = load_toggle_key()) {
        return *saved;
    }
    std::cout << "Press the key that will turn the bot on and off...\n";
    const int key = wait_for_key_press();
    save_toggle_key(key);
    std::cout << "Saved. Delete " << kToggleKeyPath << " to choose another key.\n";
    return key;
}

// Ловит момент нажатия клавиши: сработает один раз, сколько бы её ни держали.
class KeyPressDetector {
public:
    explicit KeyPressDetector(int key) : key_(key), was_down_(is_key_down(key)) {}

    // true, если клавиша нажата с прошлой проверки.
    bool pressed() {
        const bool down = is_key_down(key_);
        const bool just_pressed = down && !was_down_;
        was_down_ = down;
        return just_pressed;
    }

private:
    int key_;
    bool was_down_;
};

} // namespace

int main() {
    // Работаем в физических пикселях, иначе при масштабе Windows не 100% съедут захват и клики.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    try {
        const int toggle_key = get_toggle_key();

        // Модель грузим один раз, это самая долгая операция.
        rocks::Detector detector(kModelPath, kMinScore);

        KeyPressDetector toggle(toggle_key);
        bool enabled = false;
        // Переключает вкл/выкл по нажатию бинда и пикает ('\a').
        const auto check_toggle = [&] {
            if (!toggle.pressed()) {
                return;
            }
            enabled = !enabled;
            std::cout << '\a' << (enabled ? "ON" : "OFF") << std::endl;
        };

        std::cout << "Ready. Open the game and press the bind (key code " << toggle_key
                  << ") to start or stop. Close this window to exit.\n";

        for (;;) {
            check_toggle();
            if (!enabled) {
                std::this_thread::sleep_for(kKeyPollInterval);
                continue;
            }

            const rocks::ScreenFrame frame = rocks::capture_screen(kGameArea);
            const std::vector<rocks::Detection> gems = detector.detect(frame);

            // Кликаем по очереди в центр каждой рамки.
            for (const rocks::Detection& gem : gems) {
                // Бинд проверяем перед каждым кликом, чтобы остановка срабатывала сразу.
                check_toggle();
                if (!enabled) {
                    break;
                }
                const int center_x = static_cast<int>(std::lround((gem.left + gem.right) / 2.0f));
                const int center_y = static_cast<int>(std::lround((gem.top + gem.bottom) / 2.0f));
                std::cout << "  click (" << center_x << ", " << center_y << "), score "
                          << gem.score << '\n';
                rocks::click_at(center_x, center_y);
                std::this_thread::sleep_for(kDelayBetweenClicks);
            }
            if (!gems.empty()) {
                std::this_thread::sleep_for(kDelayBetweenScans);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
