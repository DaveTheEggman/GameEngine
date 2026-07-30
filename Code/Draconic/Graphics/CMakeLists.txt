# Draconic::Graphics - `draconic.graphics` (GraphicsDevice + RenderWindow
# + FrameContext: the shared RHI render host). Backend-agnostic - imports only the
# base RHI, so it builds everywhere (incl. headless, no Vulkan). Backend factories
# (Null / Vulkan) are sibling modules so heavy backend modules never enter this
# interface (GCC's module reader rejects that).
add_library(DraconicGraphics STATIC)
add_library(Draconic::Graphics ALIAS DraconicGraphics)

target_link_libraries(DraconicGraphics
    PUBLIC
        Draconic::Core
        Draconic::RHI
        Draconic::Shell
        Draconic::Policy)

target_sources(DraconicGraphics
    PUBLIC
        FILE_SET graphics_modules TYPE CXX_MODULES FILES
            Graphics.cppm)

# Draconic::GraphicsNull - `draconic.graphics.null` (headless device
# factory over the Null RHI backend). No GPU/Vulkan required.
add_library(DraconicGraphicsNull STATIC)
add_library(Draconic::GraphicsNull ALIAS DraconicGraphicsNull)

target_link_libraries(DraconicGraphicsNull
    PUBLIC
        Draconic::Core
        Draconic::RHI
        Draconic::RHINull
        Draconic::Graphics
        Draconic::Policy)

target_sources(DraconicGraphicsNull
    PUBLIC
        FILE_SET graphics_null_modules TYPE CXX_MODULES FILES
            GraphicsNull.cppm)

# Draconic::GraphicsGpu - `draconic.graphics.gpu` (the Vulkan/DX12
# backend factory). The only Vulkan-coupled part; skipped without the Vulkan RHI.
if(TARGET Draconic::RHIVulkan)
    add_library(DraconicGraphicsGpu STATIC)
    add_library(Draconic::GraphicsGpu ALIAS DraconicGraphicsGpu)

    target_link_libraries(DraconicGraphicsGpu
        PUBLIC
            Draconic::Core
            Draconic::RHI
            Draconic::RHIVulkan
            Draconic::RHIValidation
            Draconic::Graphics
            Draconic::GraphicsNull
            Draconic::Policy)

    if(TARGET Draconic::RHIDX12)
        target_link_libraries(DraconicGraphicsGpu PUBLIC Draconic::RHIDX12)
        target_compile_definitions(DraconicGraphicsGpu PUBLIC DRACONIC_HAS_DX12)
    endif()

    if(TARGET Draconic::RHIWebGPU)
        target_link_libraries(DraconicGraphicsGpu PUBLIC Draconic::RHIWebGPU)
        target_compile_definitions(DraconicGraphicsGpu PUBLIC DRACONIC_HAS_WEBGPU)
    endif()

    target_sources(DraconicGraphicsGpu
        PUBLIC
            FILE_SET graphics_gpu_modules TYPE CXX_MODULES FILES
                GraphicsGpu.cppm
        PRIVATE
            GraphicsGpu.cpp)   # implementation unit: backend imports live here, off consumers' closure
else()
    message(STATUS "Vulkan RHI not available - skipping Draconic::GraphicsGpu")
endif()

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
