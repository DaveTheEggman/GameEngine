add_executable(Navigation.Tests
    TestsMain.cpp
    NavigationTests.cpp)

target_link_libraries(Navigation.Tests PRIVATE
    Foundation::Navigation doctest::doctest)

target_compile_definitions(Navigation.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Navigation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Navigation.Tests COMMAND Navigation.Tests)
