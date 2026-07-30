add_executable(DraconicSmoketest Main.cpp)
target_link_libraries(DraconicSmoketest PRIVATE
    Draconic::RHIVulkan Draconic::RHINull Draconic::RHIValidation Draconic::Shaders
    Draconic::ShellDesktop Draconic::RuntimeDesktop Draconic::Core Draconic::Policy)
target_compile_definitions(DraconicSmoketest PRIVATE DRACONIC_HAS_SHADERS)

# DX12 backend - Windows only.
if(TARGET Draconic::RHIDX12)
    target_link_libraries(DraconicSmoketest PRIVATE Draconic::RHIDX12)
    target_compile_definitions(DraconicSmoketest PRIVATE DRACONIC_HAS_DX12)
endif()

# WebGPU backend - present when the wgpu headers are vendored; the sidecar itself
# is probed at run time (section prints "skipped" when unavailable).
if(TARGET Draconic::RHIWebGPU)
    target_link_libraries(DraconicSmoketest PRIVATE Draconic::RHIWebGPU)
    target_compile_definitions(DraconicSmoketest PRIVATE DRACONIC_HAS_WEBGPU)
endif()

target_compile_options(DraconicSmoketest PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,GNU>:-Wno-error=unused-parameter;-Wno-error=unused-variable;-Wno-error=missing-field-initializers;-Wno-error=misleading-indentation>")

# Copy SDL3.dll next to the executable on Windows.
draconic_copy_runtime_deps(DraconicSmoketest EXTRA ${DRACONIC_SDL3_DLL})
