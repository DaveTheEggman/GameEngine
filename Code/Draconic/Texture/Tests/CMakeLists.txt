
add_executable(DraconicTextureTests TestsMain.cpp TextureDataTests.cpp)
target_link_libraries(DraconicTextureTests PRIVATE Draconic::Texture Draconic::RHI Draconic::Image doctest::doctest)
target_compile_definitions(DraconicTextureTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicTextureTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicTextureTests COMMAND DraconicTextureTests)
