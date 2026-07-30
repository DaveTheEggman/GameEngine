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

#if DRACONIC_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

export module draconic.rhi.webgpu:api;

import draconic.core;

using namespace draconic::core;

namespace draconic::rhi::webgpu
{
    // Every wgpu entry point the backend uses - the ONE list both load paths walk.
    // decltype(&::fn) works because webgpu.h/wgpu.h DECLARE the functions; nothing
    // references them directly, so no link-time dependency exists.
#define DRACONIC_WEBGPU_FUNCTIONS(X)                                                               \
    X(wgpuCreateInstance)                                                                          \
    X(wgpuInstanceRelease)                                                                         \
    X(wgpuInstanceProcessEvents)                                                                   \
    X(wgpuInstanceRequestAdapter)                                                                  \
    X(wgpuInstanceEnumerateAdapters) /* wgpu-native extension (desktop only) */                    \
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
    X(wgpuRenderPassEncoderSetImmediates) /* Immediates feature (push constants) */                \
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
    X(wgpuComputePassEncoderSetImmediates) /* Immediates feature (push constants) */               \
    X(wgpuComputePassEncoderDispatchWorkgroups)                                                    \
    X(wgpuComputePassEncoderDispatchWorkgroupsIndirect)                                            \
    X(wgpuComputePassEncoderEnd)                                                                   \
    X(wgpuComputePassEncoderRelease)                                                               \
    X(wgpuDeviceCreateRenderBundleEncoder)                                                         \
    X(wgpuRenderBundleEncoderSetPipeline)                                                          \
    X(wgpuRenderBundleEncoderSetBindGroup)                                                         \
    X(wgpuRenderBundleEncoderSetImmediates) /* Immediates feature (push constants) */              \
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
    X(wgpuSurfaceUnconfigure)                                                                      \
    X(wgpuSurfaceGetCurrentTexture)                                                                \
    X(wgpuSurfacePresent)                                                                          \
    X(wgpuSurfaceRelease)                                                                          \
    X(wgpuDeviceRelease)                                                                           \
    X(wgpuDevicePoll) /* wgpu-native extension (desktop only) */                                   \
    X(wgpuQueueSubmit)                                                                             \
    X(wgpuQueueSubmitForIndex) /* wgpu-native extension: index for precise waits */                \
    X(wgpuQueueOnSubmittedWorkDone)                                                                \
    X(wgpuQueueGetTimestampPeriod) /* wgpu-native extension (desktop only) */                      \
    X(wgpuQueueRelease)                                                                            \
    X(wgpuSetLogCallback) /* wgpu-native extension (desktop only) */

    export struct WebGpuApi
    {
#define DRACONIC_WEBGPU_DECLARE_MEMBER(fn) decltype(&::fn) fn = nullptr;
        DRACONIC_WEBGPU_FUNCTIONS(DRACONIC_WEBGPU_DECLARE_MEMBER)
#undef DRACONIC_WEBGPU_DECLARE_MEMBER

        void* libraryHandle = nullptr; // desktop sidecar handle; null on web
        // Whether the instance was created with ShaderSourceSPIRV (the standard
        // instance feature): the desktop DXC dev loop. Browsers never have it.
        bool spirvIngestion = false;

        /// Pumps callback delivery for AllowProcessEvents-mode futures until `done`
        /// flips or the iteration guard trips. Suits creation-time waits (nothing on
        /// the GPU timeline); GPU-completion waits use PumpUntilWithDevice.
        void PumpUntil(WGPUInstance instance, const bool& done) const
        {
            for (u32 i = 0; i < 100000 && !done; ++i)
            {
                wgpuInstanceProcessEvents(instance);
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
