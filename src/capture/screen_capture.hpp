#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rocks {

// Physical pixels relative to the primary monitor's top-left corner.
struct CaptureRegion {
    int x;
    int y;
    int width;
    int height;
};

struct ScreenFrame {
    int width;
    int height;
    int desktop_x;
    int desktop_y;
    std::size_t stride;
    // Owned, tightly packed BGRA pixels. Rows run from top to bottom.
    std::vector<std::uint8_t> bgra;
};

// Windows desktop capture. Use each instance from one thread.
class ScreenCapture {
public:
    // Without a region, captures the whole primary monitor.
    explicit ScreenCapture(std::optional<CaptureRegion> region = std::nullopt);
    ~ScreenCapture();

    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Returns no frame on timeout or display-session loss. The following call
    // recreates the capture session after session loss. Other failures throw.
    [[nodiscard]] std::optional<ScreenFrame> capture(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{16});

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rocks
