// SampleApp — abstract base for RHI samples (adapted from the Draconic sample
// framework to Raptor's platform). Brings up a window (raptor.runtime.platform),
// a Vulkan backend (validation-wrapped), device, queue, and swap chain; pumps
// events, tracks timing, and calls onRender(). Resize is detected by polling the
// window size; the loop skips rendering while minimized.

module;
#include "Core/Prelude.h"
#include <chrono>
#include <cstring>

export module raptor.samples.framework:sample_app;

import raptor.core;
import raptor.rhi;
import raptor.rhi.vk;
import raptor.rhi.validation;
import raptor.runtime.platform;
import raptor.runtime.platform.desktop;

using namespace raptor::core;

export namespace raptor::samples::framework {

enum class BackendType { Vulkan, DX12 };

class SampleApp {
public:
    explicit SampleApp(BackendType backend = BackendType::Vulkan, bool validation = true)
        : backendType_(backend), validationEnabled_(validation) {}
    virtual ~SampleApp() = default;

    SampleApp(const SampleApp&)            = delete;
    SampleApp& operator=(const SampleApp&) = delete;

    int run(int argc = 0, char** argv = nullptr);

protected:
    virtual StringView          title() const { return u"Raptor Sample"; }
    virtual rhi::DeviceFeatures requiredFeatures() const { return {}; }
    virtual rhi::TextureFormat  swapChainFormat() const { return rhi::TextureFormat::RGBA8UnormSrgb; }
    virtual rhi::PresentMode    presentMode() const { return rhi::PresentMode::Fifo; }
    virtual u32                 bufferCount() const { return 2; }

    virtual Status onInit()     = 0;
    virtual void   onRender()   = 0;
    virtual void   onResize(u32, u32) {}
    virtual void   onShutdown() = 0;

    runtime::IPlatform* platform_ = nullptr;   // borrowed from platformOwner_
    runtime::IWindow*   window_   = nullptr;
    rhi::Backend*       backend_  = nullptr;
    rhi::Device*        device_   = nullptr;
    rhi::Queue*         graphicsQueue_ = nullptr;
    rhi::Surface*       surface_  = nullptr;
    rhi::SwapChain*     swapChain_ = nullptr;

    u32 width_  = 1280;
    u32 height_ = 720;
    bool running_ = false;
    f32  deltaTime_ = 0.0f;
    f32  totalTime_ = 0.0f;

    void exit() { running_ = false; }

private:
    BackendType backendType_;
    bool        validationEnabled_;
    UniquePtr<runtime::IPlatform> platformOwner_;

    Status init_();
    void   mainLoop_();
    void   shutdown_();
    Status createBackend_();
    Status createSwapChain_();
    void   checkAndResize_();
};

inline int SampleApp::run(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dx12") == 0 || std::strcmp(argv[i], "--d3d12") == 0) {
            backendType_ = BackendType::DX12;
        } else if (std::strcmp(argv[i], "--vk") == 0 || std::strcmp(argv[i], "--vulkan") == 0) {
            backendType_ = BackendType::Vulkan;
        } else if (std::strcmp(argv[i], "--novalidation") == 0) {
            validationEnabled_ = false;
        }
    }

    if (!init_().IsOk()) { shutdown_(); return 1; }
    mainLoop_();
    shutdown_();
    return 0;
}

inline Status SampleApp::init_() {
    runtime::WindowSettings ws{};
    ws.title  = title();
    ws.width  = width_;
    ws.height = height_;

    platformOwner_ = runtime::CreatePlatform(ws);
    platform_ = platformOwner_.Get();
    if (platform_ == nullptr || platform_->MainWindow() == nullptr) {
        rhi::logError("SampleApp: platform/window init failed"); return ErrorCode::Unknown;
    }
    window_ = platform_->MainWindow();
    width_  = window_->Width();
    height_ = window_->Height();

    if (!createBackend_().IsOk()) return ErrorCode::Unknown;

    const runtime::NativeWindow nw = window_->Native();
    if (!backend_->createSurface(nw.window, nw.display, surface_).IsOk()) {
        rhi::logError("SampleApp: createSurface failed"); return ErrorCode::Unknown;
    }

    auto adapters = backend_->enumerateAdapters();
    if (adapters.Size() == 0) { rhi::logError("SampleApp: no adapters"); return ErrorCode::Unknown; }
    rhi::Adapter* adapter = adapters[0];   // best GPU first

    rhi::DeviceDesc dd{};
    dd.graphicsQueueCount = 1;
    dd.requiredFeatures   = requiredFeatures();
    if (!adapter->createDevice(dd, device_).IsOk()) {
        rhi::logError("SampleApp: createDevice failed"); return ErrorCode::Unknown;
    }

    graphicsQueue_ = device_->getQueue(rhi::QueueType::Graphics);
    if (graphicsQueue_ == nullptr) { rhi::logError("SampleApp: no graphics queue"); return ErrorCode::Unknown; }

    if (!createSwapChain_().IsOk()) return ErrorCode::Unknown;
    return onInit();
}

inline Status SampleApp::createBackend_() {
    rhi::Backend* raw = nullptr;
    switch (backendType_) {
    case BackendType::Vulkan: {
        rhi::vk::VkBackendDesc desc{};
        desc.enableValidation = validationEnabled_;
        if (!rhi::vk::createBackend(desc, raw).IsOk()) {
            rhi::logError("SampleApp: vk::createBackend failed"); return ErrorCode::Unknown;
        }
        break;
    }
    default:
        rhi::logError("SampleApp: only the Vulkan backend is available on this platform");
        return ErrorCode::Unknown;
    }
    backend_ = validationEnabled_ ? rhi::validation::createValidatedBackend(raw) : raw;
    return backend_ != nullptr ? Status{} : Status{ ErrorCode::Unknown };
}

inline Status SampleApp::createSwapChain_() {
    rhi::SwapChainDesc desc{};
    desc.width       = width_;
    desc.height      = height_;
    desc.format      = swapChainFormat();
    desc.presentMode = presentMode();
    desc.bufferCount = bufferCount();
    desc.label       = u"main";
    if (!device_->createSwapChain(surface_, desc, swapChain_).IsOk()) {
        rhi::logError("SampleApp: createSwapChain failed"); return ErrorCode::Unknown;
    }
    return Status{};
}

inline void SampleApp::mainLoop_() {
    using clock = std::chrono::steady_clock;
    using dur   = std::chrono::duration<f32>;

    running_ = true;
    auto lastTime = clock::now();

    while (running_ && platform_->IsRunning()) {
        platform_->ProcessEvents();

        const auto now = clock::now();
        deltaTime_ = dur(now - lastTime).count();
        lastTime   = now;
        totalTime_ += deltaTime_;

        if (!window_->IsMinimized()) {
            checkAndResize_();
            onRender();
        }
    }
}

inline void SampleApp::checkAndResize_() {
    const u32 nw = window_->Width();
    const u32 nh = window_->Height();
    if (nw == 0 || nh == 0) return;
    if (nw == width_ && nh == height_) return;
    width_ = nw; height_ = nh;
    device_->waitIdle();
    swapChain_->resize(width_, height_);
    onResize(width_, height_);
}

inline void SampleApp::shutdown_() {
    if (device_) device_->waitIdle();
    onShutdown();
    if (swapChain_) { device_->destroySwapChain(swapChain_); swapChain_ = nullptr; }
    if (surface_)   { device_->destroySurface(surface_);     surface_   = nullptr; }
    if (device_)    { device_->destroy();                    device_    = nullptr; }
    if (backend_)   { backend_->destroy();                   backend_   = nullptr; }
    platformOwner_.Reset();   // destroys the window + platform
}

} // namespace raptor::samples::framework
