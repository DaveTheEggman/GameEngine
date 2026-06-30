# Draconic::RHI — the `draconic.rhi` module (the backend-agnostic RHI interface).
# Ported from the Draconic RHI, adapted to Draconic types (draconic.core) and the
# minimal-STL / -fno-exceptions / -fno-rtti policy. Backends (Null, Vulkan, DX12,
# Validation) are separate targets.

add_library(DraconicRHI STATIC)
add_library(Draconic::RHI ALIAS DraconicRHI)

target_link_libraries(DraconicRHI PUBLIC Draconic::Core Draconic::Policy)

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

# Vulkan backend — built when the Vulkan SDK is available.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_subdirectory(Vulkan)
else()
    message(STATUS "Vulkan not found — skipping Draconic::RHIVulkan")
endif()

# DX12 backend — Windows only.
if(WIN32)
    add_subdirectory(DX12)
endif()
