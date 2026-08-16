add_executable(Editor.PropertyAnimation.Tests TestsMain.cpp ClipEditorViewTests.cpp)
target_link_libraries(Editor.PropertyAnimation.Tests PRIVATE
    Editor::PropertyAnimation Editor::Core
    Foundation::PropertyAnimation Foundation::UI Foundation::UI.Toolkit
    doctest::doctest)
target_compile_definitions(Editor.PropertyAnimation.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.PropertyAnimation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.PropertyAnimation.Tests COMMAND Editor.PropertyAnimation.Tests)
