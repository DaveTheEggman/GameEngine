# Integration.TextureCompression - the block-compression path exercised end to end on REAL GPU
# backends: encode with the cook's encoder (Pipeline::Texture.Compression) then upload + sample the
# BC texture through the Foundation render chain (Vulkan + WebGPU). Cross-collection flow (Pipeline
# encoder x Foundation render), so it lives in Code/Integration, not a Foundation-tier test. Skips
# cleanly when no GPU backend is available.
add_executable(Integration.TextureCompression TestsMain.cpp BcTextureProbeTests.cpp)

target_link_libraries(Integration.TextureCompression PRIVATE
    Foundation::Render Foundation::Materials.PipelineCache Foundation::RHI.TestSupport
    Pipeline::Texture.Compression doctest::doctest)

# Link each GPU backend that is present and flag it (OPTION_HAS_*); the probe iterates whichever
# are compiled in. None is hardcoded, so this builds on any platform (Vulkan+WebGPU on Linux,
# DX12+WebGPU on Windows).
if(TARGET Foundation::RHI.Vulkan)
    target_link_libraries(Integration.TextureCompression PRIVATE Foundation::RHI.Vulkan)
    target_compile_definitions(Integration.TextureCompression PRIVATE OPTION_HAS_VULKAN)
endif()
if(TARGET Foundation::RHI.WebGPU)
    target_link_libraries(Integration.TextureCompression PRIVATE Foundation::RHI.WebGPU)
    target_compile_definitions(Integration.TextureCompression PRIVATE OPTION_HAS_WEBGPU)
endif()
if(TARGET Foundation::RHI.DX12)
    target_link_libraries(Integration.TextureCompression PRIVATE Foundation::RHI.DX12)
    target_compile_definitions(Integration.TextureCompression PRIVATE OPTION_HAS_DX12)
endif()

target_compile_definitions(Integration.TextureCompression PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")

target_compile_options(Integration.TextureCompression PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Integration.TextureCompression COMMAND Integration.TextureCompression)
