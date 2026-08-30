add_executable(Editor.Fonts.Tests TestsMain.cpp FontThumbnailTests.cpp)
target_link_libraries(Editor.Fonts.Tests PRIVATE
    Editor::Fonts Editor::Core
    Foundation::Image doctest::doctest)
target_compile_definitions(Editor.Fonts.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    TEST_FONT_PATH="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Bold.ttf")
target_compile_options(Editor.Fonts.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Fonts.Tests COMMAND Editor.Fonts.Tests)
