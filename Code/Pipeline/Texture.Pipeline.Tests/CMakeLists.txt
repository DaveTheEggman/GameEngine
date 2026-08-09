
add_executable(Texture.Pipeline.Tests TestsMain.cpp TexturePipelineTests.cpp TextureReflectionTests.cpp)
target_link_libraries(Texture.Pipeline.Tests PRIVATE
    Pipeline::Texture Foundation::Texture.Resource Foundation::Texture Editor::Core
    Foundation::RHI Foundation::RHI.Null Pipeline::Core Foundation::Image Foundation::Image.IO
    Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(Texture.Pipeline.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Texture.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Texture.Pipeline.Tests COMMAND Texture.Pipeline.Tests)
