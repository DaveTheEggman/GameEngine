add_executable(Editor.App.Tests TestsMain.cpp ShellTests.cpp LogViewTests.cpp ToolPanelTests.cpp AssetPickerSlotTests.cpp FontAtlasDiskCacheTests.cpp)
target_link_libraries(Editor.App.Tests PRIVATE Foundation::VFS Editor::App Editor::Core Editor::ViewportTools doctest::doctest)
target_compile_definitions(Editor.App.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.App.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.App.Tests COMMAND Editor.App.Tests)
