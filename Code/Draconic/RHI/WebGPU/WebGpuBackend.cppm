/// draconic.rhi.webgpu:backend - Backend entry point + factory.
///
/// Owns the loaded function table (desktop: the dlopen'd wgpu-native sidecar), the
/// WGPUInstance, and the enumerated adapters. Desktop enumeration uses wgpu-native's
/// wgpuInstanceEnumerateAdapters extension (all adapters, synchronous); the web build
/// will use the standard async wgpuInstanceRequestAdapter when the browser milestone
/// lands (the extension entries are null there).

module;
#include "Draconic.Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:backend;

import draconic.core;
import draconic.rhi;
import :api;
import :adapter;
import :surface;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    struct WebGpuBackendDesc
    {
        /// Explicit sidecar path; empty = the vendored DRACONIC_WGPU_PATH, then the
        /// bare soname (a relocated dist's $ORIGIN-staged copy).
        StringView libraryPathOverride;
    };

    class WebGpuBackend final : public Backend
    {
    public:
        explicit WebGpuBackend(IAllocator& allocator) : m_allocator(allocator) {}

        Status Initialize(const WebGpuBackendDesc& desc)
        {
            const Status loaded = LoadWebGpuApi(m_api, desc.libraryPathOverride);
            if (!loaded.IsOk())
            {
                return loaded;
            }

            // Request SPIR-V ingestion (a STANDARD instance feature) - the desktop DXC
            // dev loop rides on it. There is no usable probe (wgpuGetInstanceFeatures
            // PANICS "not implemented" in wgpu-native v29, like WaitAny), so request
            // optimistically and fall back to a plain instance when refused; the
            // browser build never requests it (no dlopen'd sidecar sets the flag).
            const WGPUInstanceFeatureName spirv = WGPUInstanceFeatureName_ShaderSourceSPIRV;
            WGPUInstanceDescriptor instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
#if !DRACONIC_PLATFORM_WEB
            instanceDesc.requiredFeatureCount = 1;
            instanceDesc.requiredFeatures = &spirv;
            m_instance = m_api.wgpuCreateInstance(&instanceDesc);
            m_api.spirvIngestion = m_instance != nullptr;
#endif
            if (m_instance == nullptr)
            {
                instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
                m_instance = m_api.wgpuCreateInstance(&instanceDesc);
            }
            (void)spirv;
            if (m_instance == nullptr)
            {
                LogError("WebGpuBackend: wgpuCreateInstance returned null");
                UnloadWebGpuApi(m_api);
                return ErrorCode::Unknown;
            }

            EnumerateNow();
            return ErrorCode::Ok;
        }

        [[nodiscard]] const WebGpuApi& Api() const { return m_api; }
        [[nodiscard]] WGPUInstance Instance() const { return m_instance; }

        Span<Adapter* const> EnumerateAdapters() override
        {
            return Span<Adapter* const>(m_adapters.Data(), m_adapters.Size());
        }

        Status CreateSurface(void* windowHandle, void* displayHandle, Surface*& out,
                             SurfacePlatform platform = SurfacePlatform::Unknown) override
        {
            out = nullptr;
            WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;

#if DRACONIC_PLATFORM_WEB
            // Web has one surface source: an HTML <canvas>, addressed by CSS selector. The
            // desktop WSI descriptors (Xlib/Wayland/HWND) do not exist in Dawn's webgpu.h.
            // windowHandle carries the selector string when the shell supplies one; otherwise
            // fall back to "#canvas" (Emscripten's default target). displayHandle is unused.
            (void)displayHandle;
            (void)platform;
            const char* selector =
                windowHandle != nullptr ? static_cast<const char*>(windowHandle) : "#canvas";
            WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas =
                WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
            canvas.selector = WGPUStringView{selector, WGPU_STRLEN};
            surfaceDesc.nextInChain = &canvas.chain;
#else
            // Exactly one platform source chains in (handle contract: Device.cppm:62).
            WGPUSurfaceSourceXlibWindow xlib = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
            WGPUSurfaceSourceWaylandSurface wayland = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
            WGPUSurfaceSourceWindowsHWND win32 = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
            switch (platform)
            {
            case SurfacePlatform::X11:
                xlib.display = displayHandle;
                xlib.window = reinterpret_cast<u64>(windowHandle); // XID smuggled as void*
                surfaceDesc.nextInChain = &xlib.chain;
                break;
            case SurfacePlatform::Wayland:
                wayland.display = displayHandle;
                wayland.surface = windowHandle;
                surfaceDesc.nextInChain = &wayland.chain;
                break;
            case SurfacePlatform::Win32:
                win32.hwnd = windowHandle;
                surfaceDesc.nextInChain = &win32.chain;
                break;
            default:
                // Unknown = best guess: X11 when a display came along, else fail.
                if (displayHandle != nullptr)
                {
                    xlib.display = displayHandle;
                    xlib.window = reinterpret_cast<u64>(windowHandle);
                    surfaceDesc.nextInChain = &xlib.chain;
                }
                else
                {
                    return ErrorCode::NotSupported;
                }
                break;
            }
#endif

            const WGPUSurface handle =
                m_api.wgpuInstanceCreateSurface(m_instance, &surfaceDesc);
            if (handle == nullptr)
            {
                return ErrorCode::Unknown;
            }
            auto* surface = m_allocator.New<WebGpuSurface>();
            surface->Adopt(m_api, handle);
            out = surface;
            return ErrorCode::Ok;
        }

        void Destroy() override
        {
            for (Adapter* adapter : m_adapters)
            {
                auto* wrapped = static_cast<WebGpuAdapter*>(adapter);
                m_api.wgpuAdapterRelease(wrapped->Handle());
                m_allocator.Delete(wrapped);
            }
            m_adapters.Clear();
            if (m_instance != nullptr)
            {
                m_api.wgpuInstanceRelease(m_instance);
                m_instance = nullptr;
            }
            UnloadWebGpuApi(m_api);
            IAllocator& allocator = m_allocator;
            this->~WebGpuBackend();
            allocator.Free(this);
        }

    private:
        void EnumerateNow()
        {
            if (m_api.wgpuInstanceEnumerateAdapters != nullptr)
            {
                // Desktop (wgpu-native extension): count call, then fill.
                const usize count =
                    m_api.wgpuInstanceEnumerateAdapters(m_instance, nullptr, nullptr);
                Array<WGPUAdapter> handles;
                handles.Resize(count);
                m_api.wgpuInstanceEnumerateAdapters(m_instance, nullptr, handles.Data());
                for (WGPUAdapter handle : handles)
                {
                    m_adapters.PushBack(
                        m_allocator.New<WebGpuAdapter>(m_api, m_instance, handle, m_allocator));
                }
            }
            else
            {
                // Standard path (web): one async request for the default adapter.
                WGPUAdapter handle = nullptr;
                bool done = false;
                struct Result
                {
                    WGPUAdapter* adapter;
                    bool* done;
                    u32 status;
                } result{&handle, &done, 0u};
                WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
#if DRACONIC_PLATFORM_WEB
                // Dawn (emdawnwebgpu) returns NO adapter when featureLevel is left Undefined - it
                // must be an explicit level. wgpu-native (desktop) does not use this field, so the
                // request is web-gated. Core = the full (non-compatibility) WebGPU feature set.
                options.featureLevel = WGPUFeatureLevel_Core;
#endif
                WGPURequestAdapterCallbackInfo callback =
                    WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
                callback.mode = WGPUCallbackMode_AllowProcessEvents;
                callback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                       WGPUStringView message, void* userdata1, void*)
                {
                    auto* r = static_cast<Result*>(userdata1);
                    r->status = static_cast<u32>(status);
                    if (status == WGPURequestAdapterStatus_Success)
                    {
                        *r->adapter = adapter;
                    }
                    else if (message.data != nullptr)
                    {
                        // Log the reason Dawn gave (valid only inside the callback).
                        ConsoleWriteError(u8"WebGpuBackend: requestAdapter message: ");
                        if (message.length != WGPU_STRLEN && message.length > 0)
                        {
                            ConsoleWriteError(StringView(
                                reinterpret_cast<const utf8char*>(message.data), message.length));
                        }
                        else
                        {
                            ConsoleWriteError(reinterpret_cast<const utf8char*>(message.data));
                        }
                        ConsoleWriteError(u8"\n");
                    }
                    *r->done = true;
                };
                callback.userdata1 = &result;
                (void)m_api.wgpuInstanceRequestAdapter(m_instance, &options, callback);
                m_api.PumpUntil(m_instance, done);
                if (!done)
                {
                    // The callback never fired: the pump gave up before the browser resolved the
                    // requestAdapter promise (an async-yield / ASYNCIFY problem, not a GPU one).
                    LogError("WebGpuBackend: requestAdapter callback did not fire (pump timed out)");
                }
                else if (handle == nullptr)
                {
                    // status: 2=CallbackCancelled, 3=Unavailable, 4=Error (see WGPURequestAdapterStatus)
                    const utf8char* name = result.status == 2u   ? u8"CallbackCancelled"
                                           : result.status == 3u ? u8"Unavailable"
                                           : result.status == 4u ? u8"Error"
                                                                 : u8"(unknown)";
                    ConsoleWriteError(u8"WebGpuBackend: requestAdapter returned no adapter, status=");
                    ConsoleWriteError(name);
                    ConsoleWriteError(u8"\n");
                }
                if (handle != nullptr)
                {
                    m_adapters.PushBack(
                        m_allocator.New<WebGpuAdapter>(m_api, m_instance, handle, m_allocator));
                }
            }
            SortAdaptersByPreference(m_adapters);
        }

        IAllocator& m_allocator;
        WebGpuApi m_api;
        WGPUInstance m_instance = nullptr;
        Array<Adapter*> m_adapters;
    };

    /// Creates the WebGPU backend. Fails with NotFound when the wgpu-native sidecar is
    /// absent (desktop) - callers treat that as "backend unavailable", same as a
    /// missing Vulkan driver.
    Status CreateBackend(const WebGpuBackendDesc& desc, Backend*& out,
                         IAllocator& allocator = DefaultAllocator())
    {
        out = nullptr;
        auto* backend = allocator.New<WebGpuBackend>(allocator);
        const Status status = backend->Initialize(desc);
        if (!status.IsOk())
        {
            IAllocator& alloc = allocator;
            backend->~WebGpuBackend();
            alloc.Free(backend);
            return status;
        }
        backend->isInitialized = true;
        out = backend;
        return ErrorCode::Ok;
    }
}
