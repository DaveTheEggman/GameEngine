# Foundation::Fonts.DistanceField unit tests (doctest).

add_executable(Fonts.DistanceField.Tests TestsMain.cpp DistanceFieldFontAtlasTests.cpp)
target_link_libraries(Fonts.DistanceField.Tests PRIVATE Foundation::Fonts.DistanceField Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.DistanceField.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Fonts.DistanceField.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.DistanceField.Tests COMMAND Fonts.DistanceField.Tests)
