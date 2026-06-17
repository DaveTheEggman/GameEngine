// SampleApp — abstract base for RHI samples (adapted from the Draconic sample
// framework to Raptor's platform). Brings up a window (raptor.runtime.platform),
// a Vulkan backend (validation-wrapped), device, queue, and swap chain; pumps
// events, tracks timing, and calls onRender(). Resize is detected by polling the
// window size; the loop skips rendering while minimized.

module;
#include "Core/Prelude.h"
#include <chrono>
#include <cstdio>
#include <cstring>

export module raptor.samples.framework:sample_app;

import raptor.core;
import raptor.rhi;
import raptor.rhi.vk;
#ifdef RAPTOR_HAS_DX12
import raptor.rhi.dx12;
#endif
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
        rhi::LogError("SampleApp: platform/window init failed"); return ErrorCode::Unknown;
    }
    window_ = platform_->MainWindow();
    width_  = window_->Width();
    height_ = window_->Height();

    if (!createBackend_().IsOk()) return ErrorCode::Unknown;

    const runtime::NativeWindow nw = window_->Native();
    if (!backend_->CreateSurface(nw.window, nw.display, surface_).IsOk()) {
        rhi::LogError("SampleApp: createSurface failed"); return ErrorCode::Unknown;
    }

    auto adapters = backend_->EnumerateAdapters();
    if (adapters.Size() == 0) { rhi::LogError("SampleApp: no adapters"); return ErrorCode::Unknown; }
    rhi::Adapter* adapter = adapters[0];   // best GPU first

    {
        rhi::AdapterInfo ai = adapter->Info();
        const char* backendName = (backendType_ == BackendType::DX12) ? "DX12" : "Vulkan";
        const UTF8String name8 = ToUTF8(ai.name);
        std::printf("SampleApp: backend=%s adapter=%s\n",
                    backendName, reinterpret_cast<const char*>(name8.CStr()));
    }

    rhi::DeviceDesc dd{};
    dd.graphicsQueueCount = 1;
    dd.requiredFeatures   = requiredFeatures();
    if (!adapter->CreateDevice(dd, device_).IsOk()) {
        rhi::LogError("SampleApp: createDevice failed"); return ErrorCode::Unknown;
    }

    graphicsQueue_ = device_->GetQueue(rhi::QueueType::Graphics);
    if (graphicsQueue_ == nullptr) { rhi::LogError("SampleApp: no graphics queue"); return ErrorCode::Unknown; }

    if (!createSwapChain_().IsOk()) return ErrorCode::Unknown;
    return onInit();
}

inline Status SampleApp::createBackend_() {
    rhi::Backend* raw = nullptr;
    switch (backendType_) {
    case BackendType::Vulkan: {
        rhi::vk::VkBackendDesc desc{};
        desc.enableValidation = validationEnabled_;
        if (!rhi::vk::CreateBackend(desc, raw).IsOk()) {
            rhi::LogError("SampleApp: vk::CreateBackend failed"); return ErrorCode::Unknown;
        }
        break;
    }
    case BackendType::DX12: {
#ifdef RAPTOR_HAS_DX12
        rhi::dx12::DxBackendDesc desc{};
        desc.enableValidation = validationEnabled_;
        if (!rhi::dx12::CreateDxBackend(desc, raw).IsOk()) {
            rhi::LogError("SampleApp: dx12::CreateDxBackend failed"); return ErrorCode::Unknown;
        }
#else
        rhi::LogError("SampleApp: DX12 backend not available on this platform");
        return ErrorCode::Unknown;
#endif
        break;
    }
    }
    backend_ = validationEnabled_ ? rhi::validation::CreateValidatedBackend(raw) : raw;
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
    if (!device_->CreateSwapChain(surface_, desc, swapChain_).IsOk()) {
        rhi::LogError("SampleApp: createSwapChain failed"); return ErrorCode::Unknown;
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
    device_->WaitIdle();
    swapChain_->Resize(width_, height_);
    onResize(width_, height_);
}

inline void SampleApp::shutdown_() {
    if (device_) device_->WaitIdle();
    onShutdown();
    if (swapChain_) { device_->DestroySwapChain(swapChain_); swapChain_ = nullptr; }
    if (surface_)   { device_->DestroySurface(surface_);     surface_   = nullptr; }
    if (device_)    { device_->Destroy();                    device_    = nullptr; }
    if (backend_)   { backend_->Destroy();                   backend_   = nullptr; }
    platformOwner_.Reset();   // destroys the window + platform
}

} // namespace raptor::samples::framework
