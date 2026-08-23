# Foundation::Lod unit tests (doctest; pure math, no GPU).

add_executable(Lod.Tests TestsMain.cpp LodMathTests.cpp)
target_link_libraries(Lod.Tests PRIVATE Foundation::Lod doctest::doctest)
target_compile_definitions(Lod.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Lod.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Lod.Tests COMMAND Lod.Tests)
