add_executable(Engine.Terrain.Tests TestsMain.cpp TerrainComponentTests.cpp TerrainRendererTests.cpp
    SplatTextureCacheTests.cpp)
target_link_libraries(Engine.Terrain.Tests PRIVATE Foundation::VFS
    Engine::Terrain Foundation::Scene Foundation::Scene.Resource
    Foundation::Terrain Foundation::Terrain.Resource Foundation::Resource
    Foundation::Heightfield Foundation::RHI Foundation::RHI.Null
    Foundation::Render Foundation::Shaders Foundation::Shaders.System doctest::doctest)
target_compile_definitions(Engine.Terrain.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.Terrain.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.Terrain.Tests COMMAND Engine.Terrain.Tests)
