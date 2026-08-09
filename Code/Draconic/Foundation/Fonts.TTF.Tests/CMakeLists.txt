# Foundation::Fonts.TTF unit tests (doctest). Uses the bundled Roboto font asset.

add_executable(Fonts.TTF.Tests TestsMain.cpp TrueTypeTests.cpp UIFeaturesTests.cpp)
target_link_libraries(Fonts.TTF.Tests PRIVATE Foundation::Fonts.TTF Foundation::Fonts.IO Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.TTF.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
    DRACONIC_FONTS_ASSET_DIR="${CMAKE_SOURCE_DIR}/Data/Assets/fonts")
target_compile_options(Fonts.TTF.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.TTF.Tests COMMAND Fonts.TTF.Tests)
