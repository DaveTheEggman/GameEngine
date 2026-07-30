# Draconic::RHIWebGPU - `draconic.rhi.webgpu`: WebGPU backend over the standard
# webgpu.h. Desktop loads the wgpu-native sidecar at RUN TIME (headers-only build
# dependency); web links the browser implementation. Gated on the vendored headers.
add_library(DraconicRHIWebGPU STATIC)
add_library(Draconic::RHIWebGPU ALIAS DraconicRHIWebGPU)
target_link_libraries(DraconicRHIWebGPU
    PUBLIC Draconic::RHI Draconic::Core Draconic::Policy
    PRIVATE Draconic::WgpuHeaders)
target_sources(DraconicRHIWebGPU PUBLIC FILE_SET rhi_webgpu_modules TYPE CXX_MODULES FILES
    WebGpuModule.cppm
    WebGpuApi.cppm
    WebGpuConversions.cppm
    WebGpuBuffer.cppm
    WebGpuTexture.cppm
    WebGpuTextureView.cppm
    WebGpuSampler.cppm
    WebGpuShaderModule.cppm
    WebGpuFence.cppm
    WebGpuQueue.cppm
    WebGpuDevice.cppm
    WebGpuAdapter.cppm
    WebGpuBackend.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
