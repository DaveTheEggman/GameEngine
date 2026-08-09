# Foundation::Geometry unit tests (doctest; pure data, no GPU).

add_executable(Geometry.Tests
    TestsMain.cpp
    GeometryTests.cpp)

target_link_libraries(Geometry.Tests PRIVATE
    Foundation::Geometry doctest::doctest)

target_compile_definitions(Geometry.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Geometry.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Geometry.Tests COMMAND Geometry.Tests)
