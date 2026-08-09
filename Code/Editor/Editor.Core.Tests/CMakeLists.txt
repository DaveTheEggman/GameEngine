add_executable(Editor.Core.Tests TestsMain.cpp CommandTests.cpp ProjectTests.cpp
    ProjectRegistryTests.cpp
    ExportTests.cpp JobServiceTests.cpp ContextTests.cpp LogBufferTests.cpp ImporterTests.cpp)
target_link_libraries(Editor.Core.Tests PRIVATE Editor::Core Pipeline::Geometry Foundation::Geometry.Resource
    Engine::Render Foundation::Scene.Resource Foundation::VFS.Pak Engine::Project Foundation::Content Foundation::VFS Foundation::Settings
    Foundation::Script.Wren Pipeline::Script.Wren Pipeline::Script Foundation::Script.Resource doctest::doctest)
target_compile_definitions(Editor.Core.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Core.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Core.Tests COMMAND Editor.Core.Tests)
