#include "detector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace rocks {
namespace {

// Как кадр вписан в квадратный вход модели: масштаб и серые поля по краям.
struct Letterbox {
    float scale;
    int width;
    int height;
    int pad_x;
    int pad_y;
};

// Уменьшаем кадр с сохранением пропорций, чтобы он влез в квадрат size×size,
// и центрируем. Для 800×448 и 640: масштаб 0.8, картинка 640×358, поля по 141 сверху и снизу.
Letterbox fit_into_square(int width, int height, int size) {
    const float scale = std::min(static_cast<float>(size) / static_cast<float>(width),
                                 static_cast<float>(size) / static_cast<float>(height));
    const int fitted_width = static_cast<int>(std::lround(static_cast<float>(width) * scale));
    const int fitted_height = static_cast<int>(std::lround(static_cast<float>(height) * scale));
    return {scale, fitted_width, fitted_height,
            (size - fitted_width) / 2, (size - fitted_height) / 2};
}

// Одна координата для билинейной интерполяции: два соседних пикселя и вес второго.
struct Sample {
    int first;
    int second;
    float weight;
};

// Какой точке исходного кадра соответствует пиксель уменьшенной картинки (как в cv2.resize).
Sample sample_at(int target, float ratio, int source_size) {
    const float position = std::clamp((static_cast<float>(target) + 0.5f) * ratio - 0.5f, 0.0f,
                                      static_cast<float>(source_size - 1));
    const int first = static_cast<int>(position);
    return {first, std::min(first + 1, source_size - 1), position - static_cast<float>(first)};
}

// Готовит вход модели: уменьшает кадр, кладёт по центру серого квадрата,
// переводит BGRA в RGB, раскладывает по каналам (CHW) и нормирует в 0..1.
void fill_input(const ScreenFrame& frame, const Letterbox& box, int size,
                std::vector<float>& input) {
    const std::size_t plane = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    // Серый цвет полей 114, как при обучении в Ultralytics.
    input.assign(plane * 3, 114.0f / 255.0f);

    const float ratio_x = static_cast<float>(frame.width) / static_cast<float>(box.width);
    const float ratio_y = static_cast<float>(frame.height) / static_cast<float>(box.height);

    for (int y = 0; y < box.height; ++y) {
        const Sample sy = sample_at(y, ratio_y, frame.height);
        const std::uint8_t* row0 = frame.bgra.data() + static_cast<std::size_t>(sy.first) * frame.stride;
        const std::uint8_t* row1 = frame.bgra.data() + static_cast<std::size_t>(sy.second) * frame.stride;
        const std::size_t target_row =
            static_cast<std::size_t>(y + box.pad_y) * static_cast<std::size_t>(size);

        for (int x = 0; x < box.width; ++x) {
            const Sample sx = sample_at(x, ratio_x, frame.width);
            const std::size_t left = static_cast<std::size_t>(sx.first) * 4;
            const std::size_t right = static_cast<std::size_t>(sx.second) * 4;
            const std::size_t target = target_row + static_cast<std::size_t>(x + box.pad_x);

            for (int channel = 0; channel < 3; ++channel) {
                // В кадре порядок B, G, R; модель ждёт R, G, B.
                const std::size_t source = static_cast<std::size_t>(2 - channel);
                const float top = row0[left + source] +
                                  (row0[right + source] - row0[left + source]) * sx.weight;
                const float bottom = row1[left + source] +
                                     (row1[right + source] - row1[left + source]) * sx.weight;
                const float value = top + (bottom - top) * sy.weight;
                input[static_cast<std::size_t>(channel) * plane + target] = value / 255.0f;
            }
        }
    }
}

} // namespace

Detector::Detector(const std::filesystem::path& model_path, float min_score)
    : env_(ORT_LOGGING_LEVEL_WARNING, "rocks"),
      session_(env_, model_path.c_str(), Ort::SessionOptions{}),
      min_score_(min_score) {
    // Имена входа и выхода берём из самой модели ("images" и "output0").
    Ort::AllocatorWithDefaultOptions allocator;
    input_name_ = session_.GetInputNameAllocated(0, allocator).get();
    output_name_ = session_.GetOutputNameAllocated(0, allocator).get();

    // Ожидаем вход [1, 3, N, N].
    const std::vector<std::int64_t> shape =
        session_.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4 || shape[1] != 3 || shape[2] <= 0 || shape[2] != shape[3]) {
        throw std::runtime_error("Model must take a fixed-size square RGB image.");
    }
    input_size_ = static_cast<int>(shape[2]);
}

std::vector<Detection> Detector::detect(const ScreenFrame& frame) {
    const Letterbox box = fit_into_square(frame.width, frame.height, input_size_);
    fill_input(frame, box, input_size_, input_);

    // Оборачиваем наш буфер в тензор без копирования и запускаем модель.
    const std::array<std::int64_t, 4> input_shape{1, 3, input_size_, input_size_};
    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const Ort::Value input = Ort::Value::CreateTensor<float>(
        memory, input_.data(), input_.size(), input_shape.data(), input_shape.size());
    const char* input_names[] = {input_name_.c_str()};
    const char* output_names[] = {output_name_.c_str()};
    std::vector<Ort::Value> outputs =
        session_.Run(Ort::RunOptions{nullptr}, input_names, &input, 1, output_names, 1);

    // Выход [1, 300, 6]: до 300 рамок, у каждой x1, y1, x2, y2, уверенность, класс.
    // NMS уже встроен в модель, лишние рамки отсеивать не нужно.
    const std::vector<std::int64_t> output_shape =
        outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    if (output_shape.size() != 3 || output_shape[2] != 6) {
        throw std::runtime_error("Unexpected model output shape.");
    }
    const float* rows = outputs[0].GetTensorData<float>();

    // Перевод из координат входа модели в координаты экрана:
    // убираем поля, отменяем масштаб, обрезаем по краю кадра, сдвигаем на позицию кадра.
    const auto to_screen_x = [&](float x) {
        const float in_frame = std::clamp((x - static_cast<float>(box.pad_x)) / box.scale, 0.0f,
                                          static_cast<float>(frame.width));
        return in_frame + static_cast<float>(frame.desktop_x);
    };
    const auto to_screen_y = [&](float y) {
        const float in_frame = std::clamp((y - static_cast<float>(box.pad_y)) / box.scale, 0.0f,
                                          static_cast<float>(frame.height));
        return in_frame + static_cast<float>(frame.desktop_y);
    };

    std::vector<Detection> detections;
    for (std::int64_t i = 0; i < output_shape[1]; ++i) {
        const float* row = rows + i * 6;
        if (row[4] < min_score_) {
            continue;
        }
        detections.push_back({to_screen_x(row[0]), to_screen_y(row[1]),
                              to_screen_x(row[2]), to_screen_y(row[3]), row[4]});
    }
    return detections;
}

} // namespace rocks
