# Draconic::ShaderEditor unit tests (doctest; VFS + content DB; no GPU/DXC).

add_executable(DraconicShaderEditorTests
    TestsMain.cpp
    ShaderEditorTests.cpp)

target_link_libraries(DraconicShaderEditorTests PRIVATE
    Draconic::ShaderEditor doctest::doctest)

target_compile_definitions(DraconicShaderEditorTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicShaderEditorTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicShaderEditorTests COMMAND DraconicShaderEditorTests)
