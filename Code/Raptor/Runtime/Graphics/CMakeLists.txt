# Raptor::RuntimeGraphics — `raptor.runtime.graphics` (GraphicsDevice + RenderWindow
# + FrameContext: the shared RHI render host). Backend-agnostic — imports only the
# base RHI, so it builds everywhere (incl. headless, no Vulkan). Backend factories
# (Null / Vulkan) are sibling modules so heavy backend modules never enter this
# interface (GCC's module reader rejects that).
add_library(RaptorRuntimeGraphics STATIC)
add_library(Raptor::RuntimeGraphics ALIAS RaptorRuntimeGraphics)

target_link_libraries(RaptorRuntimeGraphics
    PUBLIC
        Raptor::Core
        Raptor::RHI
        Raptor::RuntimePlatform
        Raptor::Policy)

target_sources(RaptorRuntimeGraphics
    PUBLIC
        FILE_SET runtime_graphics_modules TYPE CXX_MODULES FILES
            Graphics.cppm)

# Raptor::RuntimeGraphicsNull — `raptor.runtime.graphics.null` (headless device
# factory over the Null RHI backend). No GPU/Vulkan required.
add_library(RaptorRuntimeGraphicsNull STATIC)
add_library(Raptor::RuntimeGraphicsNull ALIAS RaptorRuntimeGraphicsNull)

target_link_libraries(RaptorRuntimeGraphicsNull
    PUBLIC
        Raptor::Core
        Raptor::RHI
        Raptor::RHINull
        Raptor::RuntimeGraphics
        Raptor::Policy)

target_sources(RaptorRuntimeGraphicsNull
    PUBLIC
        FILE_SET runtime_graphics_null_modules TYPE CXX_MODULES FILES
            GraphicsNull.cppm)

# Raptor::RuntimeGraphicsGpu — `raptor.runtime.graphics.gpu` (the Vulkan/DX12
# backend factory). The only Vulkan-coupled part; skipped without the Vulkan RHI.
if(TARGET Raptor::RHIVulkan)
    add_library(RaptorRuntimeGraphicsGpu STATIC)
    add_library(Raptor::RuntimeGraphicsGpu ALIAS RaptorRuntimeGraphicsGpu)

    target_link_libraries(RaptorRuntimeGraphicsGpu
        PUBLIC
            Raptor::Core
            Raptor::RHI
            Raptor::RHIVulkan
            Raptor::RHIValidation
            Raptor::RuntimeGraphics
            Raptor::RuntimeGraphicsNull
            Raptor::Policy)

    if(TARGET Raptor::RHIDX12)
        target_link_libraries(RaptorRuntimeGraphicsGpu PUBLIC Raptor::RHIDX12)
        target_compile_definitions(RaptorRuntimeGraphicsGpu PUBLIC RAPTOR_HAS_DX12)
    endif()

    target_sources(RaptorRuntimeGraphicsGpu
        PUBLIC
            FILE_SET runtime_graphics_gpu_modules TYPE CXX_MODULES FILES
                GraphicsGpu.cppm)
else()
    message(STATUS "Vulkan RHI not available — skipping Raptor::RuntimeGraphicsGpu")
endif()

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
