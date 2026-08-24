add_executable(Terrain.Pipeline.Tests TestsMain.cpp TerrainPipelineTests.cpp SplatmapPipelineTests.cpp)
target_link_libraries(Terrain.Pipeline.Tests PRIVATE
    Pipeline::Terrain Foundation::Terrain.Resource
    Foundation::Heightfield Foundation::Heightfield.Resource
    Pipeline::Core Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Terrain.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Terrain.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Terrain.Pipeline.Tests COMMAND Terrain.Pipeline.Tests)
