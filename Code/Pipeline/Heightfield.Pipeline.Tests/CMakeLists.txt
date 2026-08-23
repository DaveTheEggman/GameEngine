add_executable(Heightfield.Pipeline.Tests TestsMain.cpp HeightfieldPipelineTests.cpp)
target_link_libraries(Heightfield.Pipeline.Tests PRIVATE
    Pipeline::Heightfield Foundation::Heightfield Foundation::Heightfield.Resource
    Foundation::Image Foundation::Image.IO
    Pipeline::Core Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Heightfield.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Heightfield.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Heightfield.Pipeline.Tests COMMAND Heightfield.Pipeline.Tests)
