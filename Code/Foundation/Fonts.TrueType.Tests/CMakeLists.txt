# Foundation::Fonts.TrueType unit tests (doctest). Uses the bundled Roboto font asset.

add_executable(Fonts.TrueType.Tests TestsMain.cpp TrueTypeTests.cpp UIFeaturesTests.cpp)
target_link_libraries(Fonts.TrueType.Tests PRIVATE Foundation::Fonts.TrueType Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.TrueType.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    BUILTIN_FONTS_ASSET_DIR="${CMAKE_SOURCE_DIR}/Data/Assets/fonts")
target_compile_options(Fonts.TrueType.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.TrueType.Tests COMMAND Fonts.TrueType.Tests)
