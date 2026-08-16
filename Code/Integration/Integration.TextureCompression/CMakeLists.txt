# Integration.TextureCompression - the block-compression path exercised end to end on REAL GPU
# backends: encode with the cook's encoder (Pipeline::Texture.Compression) then upload + sample the
# BC texture through the Foundation render chain (Vulkan + WebGPU). Cross-collection flow (Pipeline
# encoder x Foundation render), so it lives in Code/Integration, not a Foundation-tier test. Skips
# cleanly when no GPU backend is available.
add_executable(Integration.TextureCompression TestsMain.cpp BcTextureProbeTests.cpp)

target_link_libraries(Integration.TextureCompression PRIVATE
    Foundation::Render Foundation::Materials.PipelineCache Foundation::RHI.TestSupport
    Foundation::RHI.Vulkan Foundation::RHI.WebGPU Pipeline::Texture.Compression doctest::doctest)

# DX12 backend - Windows only. The BC probe covered Vulkan + WebGPU only, so the compressed-
# texture upload/sample path had no DX12 coverage.
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
