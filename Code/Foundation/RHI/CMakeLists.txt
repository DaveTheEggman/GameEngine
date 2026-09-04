# Foundation::RHI - the `foundation.rhi` module (the backend-agnostic RHI interface).
# Ported from the Sedulous RHI, adapted to our types (foundation.core) and the
# minimal-STL / -fno-exceptions / -fno-rtti policy. Backends (Null, Vulkan, DX12,
# Validation) are separate targets.

util_add_engine_library(RHI ALIAS Foundation::RHI)
target_link_libraries(RHI PUBLIC Foundation::Core Foundation::Policy)

# Implementation units (headers they need stay OUT of the interface gcm - GCC hygiene).
# Listed as regular sources, NOT in the CXX_MODULES file set.
target_sources(RHI PRIVATE
    ResourcesImpl.cpp)

target_sources(RHI
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
