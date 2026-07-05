# Draconic::RuntimeGraphics — `draconic.runtime.graphics` (GraphicsDevice + RenderWindow
# + FrameContext: the shared RHI render host). Backend-agnostic — imports only the
# base RHI, so it builds everywhere (incl. headless, no Vulkan). Backend factories
# (Null / Vulkan) are sibling modules so heavy backend modules never enter this
# interface (GCC's module reader rejects that).
add_library(DraconicRuntimeGraphics STATIC)
add_library(Draconic::RuntimeGraphics ALIAS DraconicRuntimeGraphics)

target_link_libraries(DraconicRuntimeGraphics
    PUBLIC
        Draconic::Core
        Draconic::RHI
        Draconic::Shell
        Draconic::Policy)

target_sources(DraconicRuntimeGraphics
    PUBLIC
        FILE_SET runtime_graphics_modules TYPE CXX_MODULES FILES
            Graphics.cppm)

# Draconic::RuntimeGraphicsNull — `draconic.runtime.graphics.null` (headless device
# factory over the Null RHI backend). No GPU/Vulkan required.
add_library(DraconicRuntimeGraphicsNull STATIC)
add_library(Draconic::RuntimeGraphicsNull ALIAS DraconicRuntimeGraphicsNull)

target_link_libraries(DraconicRuntimeGraphicsNull
    PUBLIC
        Draconic::Core
        Draconic::RHI
        Draconic::RHINull
        Draconic::RuntimeGraphics
        Draconic::Policy)

target_sources(DraconicRuntimeGraphicsNull
    PUBLIC
        FILE_SET runtime_graphics_null_modules TYPE CXX_MODULES FILES
            GraphicsNull.cppm)

# Draconic::RuntimeGraphicsGpu — `draconic.runtime.graphics.gpu` (the Vulkan/DX12
# backend factory). The only Vulkan-coupled part; skipped without the Vulkan RHI.
if(TARGET Draconic::RHIVulkan)
    add_library(DraconicRuntimeGraphicsGpu STATIC)
    add_library(Draconic::RuntimeGraphicsGpu ALIAS DraconicRuntimeGraphicsGpu)

    target_link_libraries(DraconicRuntimeGraphicsGpu
        PUBLIC
            Draconic::Core
            Draconic::RHI
            Draconic::RHIVulkan
            Draconic::RHIValidation
            Draconic::RuntimeGraphics
            Draconic::RuntimeGraphicsNull
            Draconic::Policy)

    if(TARGET Draconic::RHIDX12)
        target_link_libraries(DraconicRuntimeGraphicsGpu PUBLIC Draconic::RHIDX12)
        target_compile_definitions(DraconicRuntimeGraphicsGpu PUBLIC DRACONIC_HAS_DX12)
    endif()

    target_sources(DraconicRuntimeGraphicsGpu
        PUBLIC
            FILE_SET runtime_graphics_gpu_modules TYPE CXX_MODULES FILES
                GraphicsGpu.cppm)
else()
    message(STATUS "Vulkan RHI not available — skipping Draconic::RuntimeGraphicsGpu")
endif()

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
