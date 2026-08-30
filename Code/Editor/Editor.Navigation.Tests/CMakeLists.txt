add_executable(Editor.Navigation.Tests TestsMain.cpp NavigationBakeTests.cpp)
target_link_libraries(Editor.Navigation.Tests PRIVATE
    Editor::Navigation Engine::Navigation Engine::Render Foundation::Geometry
    Engine::Terrain Foundation::Terrain.Resource Foundation::Heightfield
    Foundation::Navigation Foundation::Navigation.Resource Pipeline::Navigation
    Foundation::Scene Foundation::Content Foundation::Resource Foundation::VFS
    Foundation::Core doctest::doctest)
target_compile_definitions(Editor.Navigation.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Navigation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Navigation.Tests COMMAND Editor.Navigation.Tests)
