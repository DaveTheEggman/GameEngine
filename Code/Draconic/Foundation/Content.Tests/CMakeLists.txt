# Foundation::Content unit tests (doctest).

add_executable(Content.Tests TestsMain.cpp ContentTests.cpp)
target_link_libraries(Content.Tests PRIVATE Foundation::Content Foundation::Xml.Serialization doctest::doctest)
target_compile_definitions(Content.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Content.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Content.Tests COMMAND Content.Tests)
