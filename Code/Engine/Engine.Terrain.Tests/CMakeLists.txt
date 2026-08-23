add_executable(Engine.Terrain.Tests TestsMain.cpp TerrainComponentTests.cpp)
target_link_libraries(Engine.Terrain.Tests PRIVATE
    Engine::Terrain Foundation::Scene Foundation::Scene.Resource
    Foundation::Terrain.Resource Foundation::Resource doctest::doctest)
target_compile_definitions(Engine.Terrain.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.Terrain.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.Terrain.Tests COMMAND Engine.Terrain.Tests)
