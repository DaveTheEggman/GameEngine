add_executable(Editor.Terrain.Tests TestsMain.cpp TerrainEditorPageTests.cpp)
target_link_libraries(Editor.Terrain.Tests PRIVATE
    Editor::Terrain Editor::Core
    Pipeline::Terrain Foundation::Terrain Foundation::Terrain.Resource Foundation::Heightfield
    doctest::doctest)
target_compile_definitions(Editor.Terrain.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Terrain.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Terrain.Tests COMMAND Editor.Terrain.Tests)
