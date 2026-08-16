add_executable(PropertyAnimation.Resource.Tests TestsMain.cpp PropertyAnimationResourceTests.cpp)
target_link_libraries(PropertyAnimation.Resource.Tests PRIVATE
    Foundation::PropertyAnimation.Resource Foundation::PropertyAnimation Foundation::Core doctest::doctest)
target_compile_definitions(PropertyAnimation.Resource.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(PropertyAnimation.Resource.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME PropertyAnimation.Resource.Tests COMMAND PropertyAnimation.Resource.Tests)
