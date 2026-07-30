# Draconic::RHIWebGPU - `draconic.rhi.webgpu`: WebGPU backend over the standard
# webgpu.h. Desktop loads the wgpu-native sidecar at RUN TIME (the vendored
# Draconic::WgpuHeaders supply webgpu.h + wgpu.h as a headers-only build dependency);
# web gets both the header and the browser binding from Emscripten's emdawnwebgpu port.
add_library(DraconicRHIWebGPU STATIC)
add_library(Draconic::RHIWebGPU ALIAS DraconicRHIWebGPU)
target_link_libraries(DraconicRHIWebGPU
    PUBLIC Draconic::RHI Draconic::Core Draconic::Policy)

if(EMSCRIPTEN)
    # emdawnwebgpu supplies <webgpu/webgpu.h> (Dawn's spec header) at compile time and the
    # JS binding to navigator.gpu at link time. `embuilder build emdawnwebgpu` populates the
    # port cache. The flag is needed on BOTH compile and link of every consumer, so it is
    # PUBLIC. (The old --use-port=emscripten_webgpu / -sUSE_WEBGPU=1 was removed in emcc 4.)
    target_compile_options(DraconicRHIWebGPU PUBLIC "--use-port=emdawnwebgpu")
    target_link_options(DraconicRHIWebGPU PUBLIC "--use-port=emdawnwebgpu")
else()
    target_link_libraries(DraconicRHIWebGPU PRIVATE Draconic::WgpuHeaders)
endif()
target_sources(DraconicRHIWebGPU PUBLIC FILE_SET rhi_webgpu_modules TYPE CXX_MODULES FILES
    WebGpuModule.cppm
    WebGpuApi.cppm
    WebGpuConversions.cppm
    WebGpuBlitHelper.cppm
    WebGpuBuffer.cppm
    WebGpuTexture.cppm
    WebGpuTextureView.cppm
    WebGpuSampler.cppm
    WebGpuShaderModule.cppm
    WebGpuBindGroupLayout.cppm
    WebGpuBindGroup.cppm
    WebGpuPipelineLayout.cppm
    WebGpuPipelineCache.cppm
    WebGpuRenderPipeline.cppm
    WebGpuComputePipeline.cppm
    WebGpuQuerySet.cppm
    WebGpuCommandBuffer.cppm
    WebGpuRenderPassEncoder.cppm
    WebGpuComputePassEncoder.cppm
    WebGpuRenderBundleEncoder.cppm
    WebGpuCommandEncoder.cppm
    WebGpuCommandPool.cppm
    WebGpuSurface.cppm
    WebGpuSwapChain.cppm
    WebGpuTransferBatch.cppm
    WebGpuFence.cppm
    WebGpuQueue.cppm
    WebGpuDevice.cppm
    WebGpuAdapter.cppm
    WebGpuBackend.cppm)

# The WebGPU backend tests spin up a real device; under web that needs a browser canvas
# (no headless node path yet), so they are desktop-only for now.
if(DRACONIC_BUILD_TESTS AND NOT EMSCRIPTEN)
    add_subdirectory(Tests)
endif()
