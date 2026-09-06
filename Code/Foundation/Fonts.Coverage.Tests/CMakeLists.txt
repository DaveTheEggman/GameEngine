# Foundation::Fonts.Coverage unit tests (doctest).

add_executable(Fonts.Coverage.Tests TestsMain.cpp BakedFontTests.cpp)
target_link_libraries(Fonts.Coverage.Tests PRIVATE Foundation::Fonts.Coverage Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.Coverage.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Fonts.Coverage.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.Coverage.Tests COMMAND Fonts.Coverage.Tests)
