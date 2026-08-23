# Engine::Terrain backend pixel probe: render a lit terrain through the full RenderFrame chain on a
# REAL Vulkan device and read the pixels back - the pixel-level ground truth for the terrain shaders
# (the Null test proves they compile; this proves they draw correct, shaded terrain). GPU checked at
# runtime; skips cleanly when unavailable.
add_executable(Engine.Terrain.Backend.Tests
    TestsMain.cpp
    TerrainPixelProbeTests.cpp)

target_link_libraries(Engine.Terrain.Backend.Tests PRIVATE
    Engine::Terrain Foundation::Render Foundation::Terrain Foundation::Heightfield
    Foundation::Shaders Foundation::Shaders.System
    Foundation::RHI.TestSupport Foundation::RHI.Vulkan doctest::doctest)

# WebGPU when available: the terrain shaders' WGSL-cook path is the load-bearing portability bet
# (the integer Load on the R16Uint height texture), so probe it too.
if(TARGET Foundation::RHI.WebGPU)
    target_link_libraries(Engine.Terrain.Backend.Tests PRIVATE Foundation::RHI.WebGPU)
    target_compile_definitions(Engine.Terrain.Backend.Tests PRIVATE OPTION_HAS_WEBGPU)
endif()

target_compile_definitions(Engine.Terrain.Backend.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")

target_compile_options(Engine.Terrain.Backend.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Engine.Terrain.Backend.Tests COMMAND Engine.Terrain.Backend.Tests)
