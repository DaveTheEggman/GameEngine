# Samples::Framework - `draconic.samples.framework` (RHI sample base + helpers).
# Needs the Vulkan backend, the desktop shell, and the shader compiler.

if(NOT (TARGET Foundation::RHI.Vulkan AND TARGET Foundation::Shell.Desktop AND TARGET Foundation::Runtime.Desktop AND TARGET Foundation::Shaders))
    message(STATUS "Skipping Samples::Framework - needs Vulkan + Desktop shell + Shaders")
    return()
endif()

add_library(SamplesFramework STATIC)
add_library(Samples::Framework ALIAS SamplesFramework)

target_link_libraries(SamplesFramework PUBLIC
    Foundation::Core
    Foundation::RHI
    Foundation::RHI.Vulkan
    Foundation::RHI.Validation
    Foundation::Shell
    Foundation::Shell.Desktop Foundation::Runtime.Desktop
    Foundation::Shaders
    Foundation::Policy)

# DX12 backend - Windows only.
if(TARGET Foundation::RHI.DX12)
    target_link_libraries(SamplesFramework PUBLIC Foundation::RHI.DX12)
    target_compile_definitions(SamplesFramework PUBLIC DRACONIC_HAS_DX12)
endif()

if(TARGET Foundation::RHI.WebGPU)
    target_link_libraries(SamplesFramework PUBLIC Foundation::RHI.WebGPU)
    target_compile_definitions(SamplesFramework PUBLIC DRACONIC_HAS_WEBGPU)
endif()

target_sources(SamplesFramework
    PUBLIC
        FILE_SET samples_framework_modules TYPE CXX_MODULES FILES
            DepthBuffer.cppm
            ShaderHelpers.cppm
            SampleApp.cppm
            Framework.cppm)
