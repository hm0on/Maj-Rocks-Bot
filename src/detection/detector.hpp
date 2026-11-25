#pragma once

#include "capture/screen_capture.hpp"

#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <string>
#include <vector>

namespace rocks {

// Один найденный камушек. Рамка в координатах экрана (физические пиксели).
struct Detection {
    float left;
    float top;
    float right;
    float bottom;
    // Уверенность модели от 0 до 1.
    float score;
};

// Ищет камушки на кадре с помощью ONNX-модели YOLO.
// Модель грузится один раз в конструкторе, поэтому объект создаём один раз и переиспользуем.
class Detector {
public:
    // min_score: рамки с уверенностью ниже порога отбрасываются.
    explicit Detector(const std::filesystem::path& model_path, float min_score = 0.5f);

    // Возвращает все камушки на кадре. Пустой вектор, если ничего не нашлось.
    [[nodiscard]] std::vector<Detection> detect(const ScreenFrame& frame);

private:
    Ort::Env env_;
    Ort::Session session_;
    std::string input_name_;
    std::string output_name_;
    // Сторона квадратного входа модели (640).
    int input_size_ = 0;
    float min_score_;
    // Буфер входного тензора, переиспользуется между кадрами.
    std::vector<float> input_;
};

} // namespace rocks
