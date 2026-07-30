/// draconic.rhi.webgpu:backend - Backend entry point + factory.
///
/// Owns the loaded function table (desktop: the dlopen'd wgpu-native sidecar), the
/// WGPUInstance, and the enumerated adapters. Desktop enumeration uses wgpu-native's
/// wgpuInstanceEnumerateAdapters extension (all adapters, synchronous); the web build
/// will use the standard async wgpuInstanceRequestAdapter when the browser milestone
/// lands (the extension entries are null there).

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:backend;

import draconic.core;
import draconic.rhi;
import :api;
import :adapter;

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

            WGPUInstanceDescriptor instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
            m_instance = m_api.wgpuCreateInstance(&instanceDesc);
            if (m_instance == nullptr)
            {
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

        Status CreateSurface(void*, void*, Surface*&,
                             SurfacePlatform = SurfacePlatform::Unknown) override
        {
            // Arrives with the swapchain stage (platform-chained WGPUSurfaceDescriptor).
            return ErrorCode::NotSupported;
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
                } result{&handle, &done};
                WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
                WGPURequestAdapterCallbackInfo callback =
                    WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
                callback.mode = WGPUCallbackMode_AllowProcessEvents;
                callback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                       WGPUStringView, void* userdata1, void*)
                {
                    auto* r = static_cast<Result*>(userdata1);
                    if (status == WGPURequestAdapterStatus_Success)
                    {
                        *r->adapter = adapter;
                    }
                    *r->done = true;
                };
                callback.userdata1 = &result;
                (void)m_api.wgpuInstanceRequestAdapter(m_instance, &options, callback);
                m_api.PumpUntil(m_instance, done);
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
