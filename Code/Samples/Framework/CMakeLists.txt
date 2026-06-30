# Draconic::SamplesFramework — `draconic.samples.framework` (RHI sample base + helpers).
# Needs the Vulkan backend, the desktop platform, and the shader compiler.

if(NOT (TARGET Draconic::RHIVulkan AND TARGET Draconic::RuntimePlatformDesktop AND TARGET Draconic::Shaders))
    message(STATUS "Skipping Draconic::SamplesFramework — needs Vulkan + Desktop platform + Shaders")
    return()
endif()

add_library(DraconicSamplesFramework STATIC)
add_library(Draconic::SamplesFramework ALIAS DraconicSamplesFramework)

target_link_libraries(DraconicSamplesFramework PUBLIC
    Draconic::Core
    Draconic::RHI
    Draconic::RHIVulkan
    Draconic::RHIValidation
    Draconic::RuntimePlatform
    Draconic::RuntimePlatformDesktop
    Draconic::Shaders
    Draconic::Policy)

# DX12 backend — Windows only.
if(TARGET Draconic::RHIDX12)
    target_link_libraries(DraconicSamplesFramework PUBLIC Draconic::RHIDX12)
    target_compile_definitions(DraconicSamplesFramework PUBLIC DRACONIC_HAS_DX12)
endif()

target_sources(DraconicSamplesFramework
    PUBLIC
        FILE_SET samples_framework_modules TYPE CXX_MODULES FILES
            DepthBuffer.cppm
            ShaderHelpers.cppm
            SampleApp.cppm
            Framework.cppm)
