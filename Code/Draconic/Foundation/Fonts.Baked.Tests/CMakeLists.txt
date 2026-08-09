# Foundation::Fonts.Baked unit tests (doctest).

add_executable(Fonts.Baked.Tests TestsMain.cpp BakedFontTests.cpp)
target_link_libraries(Fonts.Baked.Tests PRIVATE Foundation::Fonts.Baked Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.Baked.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Fonts.Baked.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.Baked.Tests COMMAND Fonts.Baked.Tests)
