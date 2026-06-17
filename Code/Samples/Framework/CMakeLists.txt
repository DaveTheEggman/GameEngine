# Raptor::SamplesFramework — `raptor.samples.framework` (RHI sample base + helpers).
# Needs the Vulkan backend, the desktop platform, and the shader compiler.

if(NOT (TARGET Raptor::RHIVulkan AND TARGET Raptor::RuntimePlatformDesktop AND TARGET Raptor::Shaders))
    message(STATUS "Skipping Raptor::SamplesFramework — needs Vulkan + Desktop platform + Shaders")
    return()
endif()

add_library(RaptorSamplesFramework STATIC)
add_library(Raptor::SamplesFramework ALIAS RaptorSamplesFramework)

target_link_libraries(RaptorSamplesFramework PUBLIC
    Raptor::Core
    Raptor::RHI
    Raptor::RHIVulkan
    Raptor::RHIValidation
    Raptor::RuntimePlatform
    Raptor::RuntimePlatformDesktop
    Raptor::Shaders
    Raptor::Policy)

# DX12 backend — Windows only.
if(TARGET Raptor::RHIDX12)
    target_link_libraries(RaptorSamplesFramework PUBLIC Raptor::RHIDX12)
    target_compile_definitions(RaptorSamplesFramework PUBLIC RAPTOR_HAS_DX12)
endif()

target_sources(RaptorSamplesFramework
    PUBLIC
        FILE_SET samples_framework_modules TYPE CXX_MODULES FILES
            DepthBuffer.cppm
            ShaderHelpers.cppm
            SampleApp.cppm
            Framework.cppm)
