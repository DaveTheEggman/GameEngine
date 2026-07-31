# Draconic::RHI - the `draconic.rhi` module (the backend-agnostic RHI interface).
# Ported from the Draconic RHI, adapted to Draconic types (draconic.core) and the
# minimal-STL / -fno-exceptions / -fno-rtti policy. Backends (Null, Vulkan, DX12,
# Validation) are separate targets.

add_library(Draconic.RHI STATIC)
add_library(Draconic::RHI ALIAS Draconic.RHI)

target_link_libraries(Draconic.RHI PUBLIC Draconic::Core Draconic::Policy)

# Implementation units (headers they need stay OUT of the interface gcm - GCC hygiene).
# Listed as regular sources, NOT in the CXX_MODULES file set.
target_sources(Draconic.RHI PRIVATE
    ResourcesImpl.cpp)

target_sources(Draconic.RHI
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
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.RHI.Null ${CMAKE_CURRENT_BINARY_DIR}/Null)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.RHI.Validation ${CMAKE_CURRENT_BINARY_DIR}/Validation)

# Vulkan backend - built when the Vulkan SDK is available.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.RHI.Vulkan ${CMAKE_CURRENT_BINARY_DIR}/Vulkan)
else()
    message(STATUS "Vulkan not found - skipping Draconic::RHIVulkan")
endif()

# DX12 backend - Windows only.
if(WIN32)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.RHI.DX12 ${CMAKE_CURRENT_BINARY_DIR}/DX12)
endif()

# WebGPU backend. Desktop loads the vendored wgpu-native sidecar at run time (the current
# implementation links wgpuXxx as external symbols resolved via that library). Emscripten instead
# provides WebGPU from the browser through the emdawnwebgpu port (Dawn's webgpu.h + a JS binding
# that speaks the real navigator.gpu); WebGPU/CMakeLists.txt wires that port for the web build.
if(EMSCRIPTEN OR TARGET Draconic::WgpuHeaders)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.RHI.WebGPU ${CMAKE_CURRENT_BINARY_DIR}/WebGPU)
else()
    message(STATUS "Skipping Draconic::RHIWebGPU (no wgpu headers)")
endif()
