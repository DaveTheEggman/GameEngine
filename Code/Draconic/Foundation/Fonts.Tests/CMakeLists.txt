# Foundation::Fonts unit tests (doctest).

add_executable(Fonts.Tests TestsMain.cpp FontsTypesTests.cpp)
target_link_libraries(Fonts.Tests PRIVATE Foundation::Fonts doctest::doctest)
target_compile_definitions(Fonts.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Fonts.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Fonts.Tests COMMAND Fonts.Tests)
