
add_executable(DraconicTextureFactoryTests TestsMain.cpp TextureFactoryTests.cpp)
target_link_libraries(DraconicTextureFactoryTests PRIVATE
    Foundation::Texture.Resource Foundation::Texture Foundation::RHI Foundation::RHI.Null
    Foundation::Content Foundation::Resource Foundation::VFS doctest::doctest)
target_compile_definitions(DraconicTextureFactoryTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicTextureFactoryTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicTextureFactoryTests COMMAND DraconicTextureFactoryTests)
