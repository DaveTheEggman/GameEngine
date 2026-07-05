# Draconic::ShaderResource unit tests (doctest; real DXC + Null RHI + content DB).

add_executable(DraconicShaderResourceTests
    TestsMain.cpp
    ShaderResourceTests.cpp)

target_link_libraries(DraconicShaderResourceTests PRIVATE
    Draconic::ShaderResource Draconic::RHINull Draconic::VFS doctest::doctest)

target_compile_definitions(DraconicShaderResourceTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicShaderResourceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicShaderResourceTests COMMAND DraconicShaderResourceTests)
