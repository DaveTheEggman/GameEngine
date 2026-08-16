add_executable(PropertyAnimation.Pipeline.Tests TestsMain.cpp PropertyAnimationPipelineTests.cpp)
target_link_libraries(PropertyAnimation.Pipeline.Tests PRIVATE
    Pipeline::PropertyAnimation Foundation::PropertyAnimation Foundation::PropertyAnimation.Resource
    Foundation::Content Foundation::Resource Foundation::VFS Pipeline::Core doctest::doctest)
target_compile_definitions(PropertyAnimation.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(PropertyAnimation.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME PropertyAnimation.Pipeline.Tests COMMAND PropertyAnimation.Pipeline.Tests)
