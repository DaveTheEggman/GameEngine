add_executable(PropertyAnimation.Tests TestsMain.cpp PropertyAnimationTests.cpp)
target_link_libraries(PropertyAnimation.Tests PRIVATE
    Foundation::PropertyAnimation Foundation::Core doctest::doctest)
target_compile_definitions(PropertyAnimation.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(PropertyAnimation.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME PropertyAnimation.Tests COMMAND PropertyAnimation.Tests)
