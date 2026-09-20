add_executable(Editor.Spline.Tests TestsMain.cpp SplineEditToolTests.cpp)
target_link_libraries(Editor.Spline.Tests PRIVATE
    Editor::Spline Editor::Core Editor::ViewportTools Foundation::Scene Foundation::Spline Engine::Spline doctest::doctest)
target_compile_definitions(Editor.Spline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Spline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Spline.Tests COMMAND Editor.Spline.Tests)
