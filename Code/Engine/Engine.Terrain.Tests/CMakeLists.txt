add_executable(Engine.Terrain.Tests TestsMain.cpp TerrainComponentTests.cpp TerrainRendererTests.cpp)
target_link_libraries(Engine.Terrain.Tests PRIVATE
    Engine::Terrain Foundation::Scene Foundation::Scene.Resource
    Foundation::Terrain Foundation::Terrain.Resource Foundation::Resource
    Foundation::Heightfield Foundation::RHI Foundation::RHI.Null
    Foundation::Render Foundation::Shaders Foundation::Shaders.System doctest::doctest)
target_compile_definitions(Engine.Terrain.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    # The renderer test reads the engine built-in shaders from the source tree, like dev runs do.
    BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")
target_compile_options(Engine.Terrain.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.Terrain.Tests COMMAND Engine.Terrain.Tests)
