# Draconic::RHI - the `draconic.rhi` module (the backend-agnostic RHI interface).
# Ported from the Draconic RHI, adapted to Draconic types (draconic.core) and the
# minimal-STL / -fno-exceptions / -fno-rtti policy. Backends (Null, Vulkan, DX12,
# Validation) are separate targets.

add_library(DraconicRHI STATIC)
add_library(Draconic::RHI ALIAS DraconicRHI)

target_link_libraries(DraconicRHI PUBLIC Draconic::Core Draconic::Policy)

# Implementation units (headers they need stay OUT of the interface gcm - GCC hygiene).
# Listed as regular sources, NOT in the CXX_MODULES file set.
target_sources(DraconicRHI PRIVATE
    ResourcesImpl.cpp)

target_sources(DraconicRHI
    PUBLIC
        FILE_SET rhi_modules TYPE CXX_MODULES FILES
            Log.cppm
            Enums.cppm
            TextureFormat.cppm
            Types.cppm
            Forward.cppm
            Descriptors.cppm
            ExtDescriptors.cppm
            Resources.cppm
            Commands.cppm
            Extensions.cppm
            Queue.cppm
            SwapChain.cppm
            Device.cppm
            RhiModule.cppm)

# Backends.
add_subdirectory(Null)
add_subdirectory(Validation)

# Vulkan backend - built when the Vulkan SDK is available.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_subdirectory(Vulkan)
else()
    message(STATUS "Vulkan not found - skipping Draconic::RHIVulkan")
endif()

# DX12 backend - Windows only.
if(WIN32)
    add_subdirectory(DX12)
endif()

# WebGPU backend - built when the vendored webgpu headers are present (desktop loads
# the wgpu-native sidecar at run time; web links the browser's implementation).
if(TARGET Draconic::WgpuHeaders)
    add_subdirectory(WebGPU)
else()
    message(STATUS "WgpuNative headers not found - skipping Draconic::RHIWebGPU")
endif()
