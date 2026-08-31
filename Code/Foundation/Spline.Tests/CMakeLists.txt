add_executable(Spline.Tests TestsMain.cpp SplineTests.cpp)
target_link_libraries(Spline.Tests PRIVATE Foundation::Spline doctest::doctest)
target_compile_definitions(Spline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Spline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Spline.Tests COMMAND Spline.Tests)
