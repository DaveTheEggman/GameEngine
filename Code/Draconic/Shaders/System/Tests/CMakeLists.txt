# Draconic::ShaderSystem unit tests (doctest; real DXC + Null RHI).

add_executable(DraconicShaderSystemTests
    TestsMain.cpp
    ShaderSystemTests.cpp
    FileProviderTests.cpp)

target_link_libraries(DraconicShaderSystemTests PRIVATE
    Draconic::ShaderSystem Draconic::RHINull doctest::doctest)

target_compile_definitions(DraconicShaderSystemTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicShaderSystemTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicShaderSystemTests COMMAND DraconicShaderSystemTests)
