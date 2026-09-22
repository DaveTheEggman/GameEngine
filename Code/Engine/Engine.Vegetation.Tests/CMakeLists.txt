add_executable(Engine.Vegetation.Tests TestsMain.cpp VegetationComponentTests.cpp)
target_link_libraries(Engine.Vegetation.Tests PRIVATE
    Engine::Vegetation Engine::Terrain Foundation::Scene Foundation::Resource
    Foundation::Geometry Foundation::Materials
    Foundation::Heightfield Foundation::Terrain Foundation::Terrain.Resource Foundation::Vegetation
    Foundation::Render doctest::doctest)
target_compile_definitions(Engine.Vegetation.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.Vegetation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.Vegetation.Tests COMMAND Engine.Vegetation.Tests)
