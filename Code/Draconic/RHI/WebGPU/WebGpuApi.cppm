/// draconic.rhi.webgpu:api - the loaded WebGPU function table.
///
/// The backend never links wgpu at build time. On DESKTOP the implementation is a
/// runtime SIDECAR (wgpu-native, same pattern as DXC - see dxc-runtime-sidecar):
/// dlopen/LoadLibrary + one dlsym per used function, resolved into this table. On WEB
/// the browser provides the implementation at link time, so the table is filled with
/// the direct symbol addresses. Backend code always calls through the table, making
/// the two paths identical above this file.
///
/// ASYNC MODEL (learned the hard way): wgpu-native v29 PANICS on wgpuInstanceWaitAny
/// with a nonzero timeout ("not implemented") even though the header advertises
/// TimedWaitAny. All async completion therefore uses AllowProcessEvents callbacks +
/// a wgpuInstanceProcessEvents pump - never WaitAny.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

#if DRACONIC_PLATFORM_WEB
#include <emscripten/emscripten.h> // emscripten_sleep - yield to the browser event loop
#elif DRACONIC_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

export module draconic.rhi.webgpu:api;

import draconic.core;

using namespace draconic::core;

#if DRACONIC_PLATFORM_WEB
// Dawn's webgpu.h (the emdawnwebgpu web port) declares none of the wgpu-native extensions,
// nor the three helper types they need, so decltype(&::fn) on the NATIVE_EXT list below would
// not compile. Forward-declare just enough - at GLOBAL scope, matching where wgpu-native's
// wgpu.h puts them on desktop - that those members are well-formed function pointers. These
// symbols are never defined, never bound (LoadWebGpuApi binds only the STANDARD list on web),
// and never invoked (every call site null-guards), so no definition or link dependency exists.
using WGPUSubmissionIndex = draconic::core::u64;
struct WGPUInstanceEnumerateAdapterOptions;
using WGPULogCallback = void (*)(int, char const*, void*);
extern "C"
{
    draconic::core::usize wgpuInstanceEnumerateAdapters(WGPUInstance, WGPUInstanceEnumerateAdapterOptions const*, WGPUAdapter*);
    WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool, WGPUSubmissionIndex const*);
    WGPUSubmissionIndex wgpuQueueSubmitForIndex(WGPUQueue, draconic::core::usize, WGPUCommandBuffer const*);
    float wgpuQueueGetTimestampPeriod(WGPUQueue);
    void wgpuSetLogCallback(WGPULogCallback, void*);
    void wgpuRenderPassEncoderSetImmediates(WGPURenderPassEncoder, draconic::core::u32, void const*, draconic::core::usize);
    void wgpuComputePassEncoderSetImmediates(WGPUComputePassEncoder, draconic::core::u32, void const*, draconic::core::usize);
    void wgpuRenderBundleEncoderSetImmediates(WGPURenderBundleEncoder, draconic::core::u32, void const*, draconic::core::usize);
}
#endif

namespace draconic::rhi::webgpu
{
    // Every STANDARD wgpu entry point the backend uses - present in both the desktop
    // wgpu-native header and Dawn's webgpu.h (the emdawnwebgpu web port). decltype(&::fn)
    // works because webgpu.h DECLARES the functions; nothing references them directly, so
    // no link-time dependency exists. The wgpu-native-only extensions live in the separate
    // NATIVE_EXT list below (absent from Dawn - compiled out on web).
#define DRACONIC_WEBGPU_FUNCTIONS(X)                                                               \
    X(wgpuCreateInstance)                                                                          \
    X(wgpuInstanceRelease)                                                                         \
    X(wgpuInstanceProcessEvents)                                                                   \
    X(wgpuInstanceRequestAdapter)                                                                  \
    X(wgpuAdapterGetInfo)                                                                          \
    X(wgpuAdapterInfoFreeMembers)                                                                  \
    X(wgpuAdapterGetLimits)                                                                        \
    X(wgpuAdapterGetFeatures)                                                                      \
    X(wgpuSupportedFeaturesFreeMembers)                                                            \
    X(wgpuAdapterRequestDevice)                                                                    \
    X(wgpuAdapterRelease)                                                                          \
    X(wgpuDeviceGetQueue)                                                                          \
    X(wgpuDeviceCreateBuffer)                                                                      \
    X(wgpuBufferMapAsync)                                                                          \
    X(wgpuBufferGetConstMappedRange)                                                               \
    X(wgpuBufferUnmap)                                                                             \
    X(wgpuBufferRelease)                                                                           \
    X(wgpuQueueWriteBuffer)                                                                        \
    X(wgpuQueueWriteTexture)                                                                       \
    X(wgpuDeviceCreateTexture)                                                                     \
    X(wgpuTextureRelease)                                                                          \
    X(wgpuTextureCreateView)                                                                       \
    X(wgpuTextureViewRelease)                                                                      \
    X(wgpuDeviceCreateSampler)                                                                     \
    X(wgpuSamplerRelease)                                                                          \
    X(wgpuDeviceCreateShaderModule)                                                                \
    X(wgpuShaderModuleRelease)                                                                     \
    X(wgpuAdapterHasFeature)                                                                       \
    X(wgpuDeviceCreateBindGroupLayout)                                                             \
    X(wgpuBindGroupLayoutRelease)                                                                  \
    X(wgpuDeviceCreateBindGroup)                                                                   \
    X(wgpuBindGroupRelease)                                                                        \
    X(wgpuDeviceCreatePipelineLayout)                                                              \
    X(wgpuPipelineLayoutRelease)                                                                   \
    X(wgpuDeviceCreateRenderPipeline)                                                              \
    X(wgpuRenderPipelineRelease)                                                                   \
    X(wgpuDeviceCreateComputePipeline)                                                             \
    X(wgpuComputePipelineRelease)                                                                  \
    X(wgpuDeviceCreateCommandEncoder)                                                              \
    X(wgpuCommandEncoderBeginRenderPass)                                                           \
    X(wgpuCommandEncoderBeginComputePass)                                                          \
    X(wgpuCommandEncoderCopyBufferToBuffer)                                                        \
    X(wgpuCommandEncoderCopyBufferToTexture)                                                       \
    X(wgpuCommandEncoderCopyTextureToBuffer)                                                       \
    X(wgpuCommandEncoderCopyTextureToTexture)                                                      \
    X(wgpuCommandEncoderWriteTimestamp)                                                            \
    X(wgpuCommandEncoderResolveQuerySet)                                                           \
    X(wgpuCommandEncoderPushDebugGroup)                                                            \
    X(wgpuCommandEncoderPopDebugGroup)                                                             \
    X(wgpuCommandEncoderInsertDebugMarker)                                                         \
    X(wgpuCommandEncoderFinish)                                                                    \
    X(wgpuCommandEncoderRelease)                                                                   \
    X(wgpuCommandBufferRelease)                                                                    \
    X(wgpuRenderPassEncoderSetPipeline)                                                            \
    X(wgpuRenderPassEncoderSetBindGroup)                                                           \
    X(wgpuRenderPassEncoderSetVertexBuffer)                                                        \
    X(wgpuRenderPassEncoderSetIndexBuffer)                                                         \
    X(wgpuRenderPassEncoderSetViewport)                                                            \
    X(wgpuRenderPassEncoderSetScissorRect)                                                         \
    X(wgpuRenderPassEncoderSetBlendConstant)                                                       \
    X(wgpuRenderPassEncoderSetStencilReference)                                                    \
    X(wgpuRenderPassEncoderDraw)                                                                   \
    X(wgpuRenderPassEncoderDrawIndexed)                                                            \
    X(wgpuRenderPassEncoderDrawIndirect)                                                           \
    X(wgpuRenderPassEncoderDrawIndexedIndirect)                                                    \
    X(wgpuRenderPassEncoderExecuteBundles)                                                         \
    X(wgpuRenderPassEncoderBeginOcclusionQuery)                                                    \
    X(wgpuRenderPassEncoderEndOcclusionQuery)                                                      \
    X(wgpuRenderPassEncoderEnd)                                                                    \
    X(wgpuRenderPassEncoderRelease)                                                                \
    X(wgpuComputePassEncoderSetPipeline)                                                           \
    X(wgpuComputePassEncoderSetBindGroup)                                                          \
    X(wgpuComputePassEncoderDispatchWorkgroups)                                                    \
    X(wgpuComputePassEncoderDispatchWorkgroupsIndirect)                                            \
    X(wgpuComputePassEncoderEnd)                                                                   \
    X(wgpuComputePassEncoderRelease)                                                               \
    X(wgpuDeviceCreateRenderBundleEncoder)                                                         \
    X(wgpuRenderBundleEncoderSetPipeline)                                                          \
    X(wgpuRenderBundleEncoderSetBindGroup)                                                         \
    X(wgpuRenderBundleEncoderSetVertexBuffer)                                                      \
    X(wgpuRenderBundleEncoderSetIndexBuffer)                                                       \
    X(wgpuRenderBundleEncoderDraw)                                                                 \
    X(wgpuRenderBundleEncoderDrawIndexed)                                                          \
    X(wgpuRenderBundleEncoderDrawIndirect)                                                         \
    X(wgpuRenderBundleEncoderDrawIndexedIndirect)                                                  \
    X(wgpuRenderBundleEncoderFinish)                                                               \
    X(wgpuRenderBundleEncoderRelease)                                                              \
    X(wgpuRenderBundleRelease)                                                                     \
    X(wgpuDeviceCreateQuerySet)                                                                    \
    X(wgpuQuerySetRelease)                                                                         \
    X(wgpuInstanceCreateSurface)                                                                   \
    X(wgpuSurfaceConfigure)                                                                        \
    X(wgpuSurfaceGetCapabilities)                                                                  \
    X(wgpuSurfaceCapabilitiesFreeMembers)                                                          \
    X(wgpuSurfaceUnconfigure)                                                                      \
    X(wgpuSurfaceGetCurrentTexture)                                                                \
    X(wgpuSurfacePresent)                                                                          \
    X(wgpuSurfaceRelease)                                                                          \
    X(wgpuDeviceRelease)                                                                           \
    X(wgpuQueueSubmit)                                                                             \
    X(wgpuQueueOnSubmittedWorkDone)                                                                \
    X(wgpuQueueRelease)

    // wgpu-native-only entry points. Present in wgpu-native's wgpu.h (desktop), ABSENT from
    // Dawn's webgpu.h (the emdawnwebgpu web port). On desktop they resolve like any other; on
    // web they stay null and every call site null-guards (SetImmediates falls back below).
#define DRACONIC_WEBGPU_NATIVE_EXT_FUNCTIONS(X)                                                    \
    X(wgpuInstanceEnumerateAdapters)                                                               \
    X(wgpuDevicePoll)                                                                              \
    X(wgpuQueueSubmitForIndex)                                                                     \
    X(wgpuQueueGetTimestampPeriod)                                                                 \
    X(wgpuSetLogCallback)                                                                          \
    X(wgpuRenderPassEncoderSetImmediates)                                                          \
    X(wgpuComputePassEncoderSetImmediates)                                                         \
    X(wgpuRenderBundleEncoderSetImmediates)

    export struct WebGpuApi
    {
#define DRACONIC_WEBGPU_DECLARE_MEMBER(fn) decltype(&::fn) fn = nullptr;
        DRACONIC_WEBGPU_FUNCTIONS(DRACONIC_WEBGPU_DECLARE_MEMBER)
        DRACONIC_WEBGPU_NATIVE_EXT_FUNCTIONS(DRACONIC_WEBGPU_DECLARE_MEMBER)
#undef DRACONIC_WEBGPU_DECLARE_MEMBER

        void* libraryHandle = nullptr; // desktop sidecar handle; null on web
        // Whether the instance was created with ShaderSourceSPIRV (the standard
        // instance feature): the desktop DXC dev loop. Browsers never have it.
        bool spirvIngestion = false;

        /// Return control to the browser event loop so its microtasks run - the ONLY way
        /// a WebGPU completion future (adapter/device/map/work-done) resolves on web, since
        /// those callbacks fire from a microtask that cannot run while wasm spins. Requires
        /// the linking executable to enable ASYNCIFY. A no-op on desktop, where wgpu-native
        /// delivers callbacks synchronously from ProcessEvents/DevicePoll - so every pump
        /// loop below is unchanged there and only gains a real progress path on web.
        void Yield() const
        {
#if DRACONIC_PLATFORM_WEB
            emscripten_sleep(0);
#endif
        }

        /// Pumps callback delivery for AllowProcessEvents-mode futures until `done`
        /// flips or the iteration guard trips. Suits creation-time waits (nothing on
        /// the GPU timeline); GPU-completion waits use PumpUntilWithDevice.
        void PumpUntil(WGPUInstance instance, const bool& done) const
        {
            for (u32 i = 0; i < 100000 && !done; ++i)
            {
                wgpuInstanceProcessEvents(instance);
                Yield();
            }
        }

        /// GPU-completion pump: map/work-done callbacks only fire when the DEVICE is
        /// polled, and a bare ProcessEvents spin can starve under real frame load.
        /// DevicePoll is NON-blocking (wait=0) - wait=1 can block forever when the
        /// queue is already empty (e.g. a MapAsync pending with no submissions in
        /// flight). On web the browser's event loop delivers; ProcessEvents suffices.
        void PumpUntilWithDevice(WGPUInstance instance, WGPUDevice device,
                                 const bool& done) const
        {
            for (u32 i = 0; i < 1000000 && !done; ++i)
            {
                wgpuInstanceProcessEvents(instance);
                if (done)
                {
                    return;
                }
                if (wgpuDevicePoll != nullptr && device != nullptr)
                {
                    (void)wgpuDevicePoll(device, 0u, nullptr);
                }
                Yield(); // web: the only progress path (DevicePoll is null there)
            }
        }
    };

    /// Fills `api` - desktop: dlopen the wgpu-native sidecar (explicit path override,
    /// else the vendored DRACONIC_WGPU_PATH, else the bare soname so a relocated dist
    /// resolves the copy staged beside the executable via $ORIGIN); web: direct symbols.
    export Status LoadWebGpuApi(WebGpuApi& api, StringView libraryPathOverride);

    /// Releases the sidecar handle (no-op on web / when never loaded).
    export void UnloadWebGpuApi(WebGpuApi& api);

#if DRACONIC_PLATFORM_WEB

    Status LoadWebGpuApi(WebGpuApi& api, StringView)
    {
        // The browser (via emscripten's library_webgpu) IS the implementation; the
        // wgpu-native-extension entries stay null and must not be reached on web.
#define DRACONIC_WEBGPU_BIND_DIRECT(fn) api.fn = &::fn;
        DRACONIC_WEBGPU_FUNCTIONS(DRACONIC_WEBGPU_BIND_DIRECT)
#undef DRACONIC_WEBGPU_BIND_DIRECT
        return ErrorCode::Ok;
    }

    void UnloadWebGpuApi(WebGpuApi&) {}

#else

    Status LoadWebGpuApi(WebGpuApi& api, StringView libraryPathOverride)
    {
#if DRACONIC_PLATFORM_WINDOWS
        HMODULE lib = nullptr;
        if (!libraryPathOverride.IsEmpty())
        {
            String path(libraryPathOverride);
            lib = LoadLibraryA(reinterpret_cast<const char*>(path.CStr()));
        }
#ifdef DRACONIC_WGPU_PATH
        if (lib == nullptr)
        {
            lib = LoadLibraryA(DRACONIC_WGPU_PATH);
        }
#endif
        if (lib == nullptr)
        {
            lib = LoadLibraryA("wgpu_native.dll"); // dist: staged beside the executable
        }
        if (lib == nullptr)
        {
            return ErrorCode::NotFound;
        }
        const auto resolve = [lib](const char* name) -> void*
        { return reinterpret_cast<void*>(GetProcAddress(lib, name)); };
#else
        void* lib = nullptr;
        if (!libraryPathOverride.IsEmpty())
        {
            String path(libraryPathOverride);
            lib = dlopen(reinterpret_cast<const char*>(path.CStr()), RTLD_NOW | RTLD_LOCAL);
        }
#ifdef DRACONIC_WGPU_PATH
        if (lib == nullptr)
        {
            lib = dlopen(DRACONIC_WGPU_PATH, RTLD_NOW | RTLD_LOCAL);
        }
#endif
        if (lib == nullptr)
        {
            // Relocated dist: the vendored absolute path does not exist there - the bare
            // soname searches RUNPATH ($ORIGIN => the copy staged beside the executable).
            lib = dlopen("libwgpu_native.so", RTLD_NOW | RTLD_LOCAL);
        }
        if (lib == nullptr)
        {
            return ErrorCode::NotFound;
        }
        const auto resolve = [lib](const char* name) -> void* { return dlsym(lib, name); };
#endif

        bool allResolved = true;
#define DRACONIC_WEBGPU_RESOLVE(fn)                                                                \
    api.fn = reinterpret_cast<decltype(&::fn)>(resolve(#fn));                                      \
    allResolved = allResolved && api.fn != nullptr;
        DRACONIC_WEBGPU_FUNCTIONS(DRACONIC_WEBGPU_RESOLVE)
        DRACONIC_WEBGPU_NATIVE_EXT_FUNCTIONS(DRACONIC_WEBGPU_RESOLVE)
#undef DRACONIC_WEBGPU_RESOLVE

        if (!allResolved)
        {
            api.libraryHandle = lib;
            UnloadWebGpuApi(api);
            return ErrorCode::NotSupported; // wrong / too-old sidecar
        }
        api.libraryHandle = lib;
        return ErrorCode::Ok;
    }

    void UnloadWebGpuApi(WebGpuApi& api)
    {
        if (api.libraryHandle == nullptr)
        {
            return;
        }
#if DRACONIC_PLATFORM_WINDOWS
        FreeLibrary(static_cast<HMODULE>(api.libraryHandle));
#else
        dlclose(api.libraryHandle);
#endif
        api.libraryHandle = nullptr;
    }

#endif // DRACONIC_PLATFORM_WEB
}
