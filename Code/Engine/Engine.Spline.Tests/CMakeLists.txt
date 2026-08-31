add_executable(Engine.Spline.Tests TestsMain.cpp SplineComponentTests.cpp)
target_link_libraries(Engine.Spline.Tests PRIVATE Engine::Spline Foundation::Spline doctest::doctest)
target_compile_definitions(Engine.Spline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Engine.Spline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Engine.Spline.Tests COMMAND Engine.Spline.Tests)
