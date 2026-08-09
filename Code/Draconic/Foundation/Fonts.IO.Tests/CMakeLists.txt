# Foundation::Fonts.IO unit tests (doctest).

add_executable(Fonts.IO.Tests TestsMain.cpp FontIOTests.cpp)
target_link_libraries(Fonts.IO.Tests PRIVATE Foundation::Fonts.IO Foundation::Fonts.Baked Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.IO.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Fonts.IO.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.IO.Tests COMMAND Fonts.IO.Tests)
