# Raptor::RHI — the `raptor.rhi` module (the backend-agnostic RHI interface).
# Ported from the Draconic RHI, adapted to Raptor types (raptor.core) and the
# minimal-STL / -fno-exceptions / -fno-rtti policy. Backends (Null, Vulkan, DX12,
# Validation) are separate targets.

add_library(RaptorRHI STATIC)
add_library(Raptor::RHI ALIAS RaptorRHI)

target_link_libraries(RaptorRHI PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorRHI
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
    message(STATUS "Vulkan not found — skipping Raptor::RHIVulkan")
endif()
