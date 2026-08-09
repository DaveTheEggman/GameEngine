
add_executable(Texture.Tests TestsMain.cpp TextureDataTests.cpp)
target_link_libraries(Texture.Tests PRIVATE Foundation::Texture Foundation::RHI Foundation::Image doctest::doctest)
target_compile_definitions(Texture.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Texture.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Texture.Tests COMMAND Texture.Tests)
