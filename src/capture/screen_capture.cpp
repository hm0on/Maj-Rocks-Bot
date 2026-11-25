#include "screen_capture.hpp"

#ifndef _WIN32
#error ScreenCapture requires Windows.
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace rocks {
namespace {

using Microsoft::WRL::ComPtr;

void check_hresult(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::system_error(
            static_cast<int>(result), std::system_category(), operation);
    }
}

struct PrimaryOutput {
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput1> output;
    DXGI_OUTPUT_DESC description;
};

PrimaryOutput find_primary_output() {
    ComPtr<IDXGIFactory1> factory;
    check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),
                  "CreateDXGIFactory1");
    const HMONITOR primary_monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

    for (UINT adapter_index = 0; ; ++adapter_index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapter_result = factory->EnumAdapters1(adapter_index, adapter.GetAddressOf());
        if (adapter_result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        check_hresult(adapter_result, "EnumAdapters1");

        for (UINT output_index = 0; ; ++output_index) {
            ComPtr<IDXGIOutput> output;
            const HRESULT output_result = adapter->EnumOutputs(output_index, output.GetAddressOf());
            if (output_result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            check_hresult(output_result, "EnumOutputs");

            DXGI_OUTPUT_DESC description{};
            check_hresult(output->GetDesc(&description), "GetDesc");
            if (description.AttachedToDesktop && description.Monitor == primary_monitor) {
                ComPtr<IDXGIOutput1> output1;
                check_hresult(output.As(&output1), "Query IDXGIOutput1");
                return {adapter, output1, description};
            }
        }
    }
    throw std::runtime_error("No primary desktop monitor is available.");
}

class AcquiredFrame {
public:
    explicit AcquiredFrame(IDXGIOutputDuplication* duplication) : duplication_(duplication) {}
    ~AcquiredFrame() { duplication_->ReleaseFrame(); }
    AcquiredFrame(const AcquiredFrame&) = delete;
    AcquiredFrame& operator=(const AcquiredFrame&) = delete;

private:
    IDXGIOutputDuplication* duplication_;
};

class MappedTexture {
public:
    MappedTexture(ID3D11DeviceContext* context, ID3D11Texture2D* texture)
        : context_(context), texture_(texture) {
        check_hresult(context_->Map(texture_, 0, D3D11_MAP_READ, 0, &data), "Map captured pixels");
    }
    ~MappedTexture() { context_->Unmap(texture_, 0); }
    MappedTexture(const MappedTexture&) = delete;
    MappedTexture& operator=(const MappedTexture&) = delete;

    D3D11_MAPPED_SUBRESOURCE data{};

private:
    ID3D11DeviceContext* context_;
    ID3D11Texture2D* texture_;
};

} // namespace

class ScreenCapture::Impl {
public:
    explicit Impl(std::optional<CaptureRegion> requested_region)
        : requested_region_(requested_region) {
        initialize();
    }

    std::optional<ScreenFrame> capture(std::chrono::milliseconds timeout) {
        if (timeout.count() < 0 || timeout.count() >= INFINITE) {
            throw std::invalid_argument("Capture timeout must be finite and nonnegative.");
        }
        if (!duplication_) {
            initialize();
        }

        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> resource;
        const HRESULT result = duplication_->AcquireNextFrame(
            static_cast<UINT>(timeout.count()), &frame_info, resource.GetAddressOf());
        if (result == DXGI_ERROR_WAIT_TIMEOUT) {
            return std::nullopt;
        }
        if (result == DXGI_ERROR_ACCESS_LOST || result == DXGI_ERROR_DEVICE_REMOVED ||
            result == DXGI_ERROR_DEVICE_RESET) {
            duplication_.Reset();
            return std::nullopt;
        }
        check_hresult(result, "AcquireNextFrame");
        const AcquiredFrame acquired_frame(duplication_.Get());

        ComPtr<ID3D11Texture2D> texture;
        check_hresult(resource.As(&texture), "Query captured texture");
        const D3D11_BOX source_box{
            static_cast<UINT>(region_.x), static_cast<UINT>(region_.y), 0,
            static_cast<UINT>(region_.x + region_.width),
            static_cast<UINT>(region_.y + region_.height), 1};
        context_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, texture.Get(), 0, &source_box);

        ScreenFrame frame{
            region_.width, region_.height,
            desktop_x_ + region_.x, desktop_y_ + region_.y,
            static_cast<std::size_t>(region_.width) * 4, {}};
        if (static_cast<std::size_t>(frame.height) >
            std::numeric_limits<std::size_t>::max() / frame.stride) {
            throw std::length_error("Captured image exceeds addressable memory.");
        }
        frame.bgra.resize(frame.stride * static_cast<std::size_t>(frame.height));
        const MappedTexture mapped(context_.Get(), staging_.Get());
        const auto* pixels = static_cast<const std::uint8_t*>(mapped.data.pData);
        for (int row = 0; row < frame.height; ++row) {
            const auto index = static_cast<std::size_t>(row);
            std::memcpy(frame.bgra.data() + index * frame.stride,
                        pixels + index * mapped.data.RowPitch, frame.stride);
        }
        return frame;
    }

private:
    void initialize() {
        const PrimaryOutput primary = find_primary_output();
        if (primary.description.Rotation != DXGI_MODE_ROTATION_IDENTITY) {
            throw std::runtime_error("Rotated monitors are not supported by ScreenCapture.");
        }
        const RECT desktop = primary.description.DesktopCoordinates;
        const int width = desktop.right - desktop.left;
        const int height = desktop.bottom - desktop.top;
        const CaptureRegion region = requested_region_.value_or(CaptureRegion{0, 0, width, height});
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            region.width > width || region.height > height ||
            region.x > width - region.width || region.y > height - region.height) {
            throw std::invalid_argument("Capture region must fit inside the primary monitor.");
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check_hresult(D3D11CreateDevice(
            primary.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            device.GetAddressOf(), nullptr, context.GetAddressOf()), "D3D11CreateDevice");
        ComPtr<IDXGIOutputDuplication> duplication;
        check_hresult(primary.output->DuplicateOutput(device.Get(), duplication.GetAddressOf()),
                      "DuplicateOutput");

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(region.width);
        description.Height = static_cast<UINT>(region.height);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        check_hresult(device->CreateTexture2D(&description, nullptr, staging.GetAddressOf()),
                      "Create staging texture");

        staging_ = staging;
        duplication_ = duplication;
        context_ = context;
        device_ = device;
        region_ = region;
        desktop_x_ = desktop.left;
        desktop_y_ = desktop.top;
    }

    std::optional<CaptureRegion> requested_region_;
    CaptureRegion region_{};
    int desktop_x_ = 0;
    int desktop_y_ = 0;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> staging_;
};

ScreenCapture::ScreenCapture(std::optional<CaptureRegion> region)
    : impl_(std::make_unique<Impl>(region)) {}

ScreenCapture::~ScreenCapture() = default;

std::optional<ScreenFrame> ScreenCapture::capture(std::chrono::milliseconds timeout) {
    return impl_->capture(timeout);
}

} // namespace rocks
