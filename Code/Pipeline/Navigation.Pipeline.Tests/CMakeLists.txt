add_executable(Navigation.Pipeline.Tests TestsMain.cpp NavigationPipelineTests.cpp)
target_link_libraries(Navigation.Pipeline.Tests PRIVATE
    Pipeline::Navigation Foundation::Navigation.Resource Foundation::Navigation Pipeline::Core
    Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Navigation.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Navigation.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Navigation.Pipeline.Tests COMMAND Navigation.Pipeline.Tests)
