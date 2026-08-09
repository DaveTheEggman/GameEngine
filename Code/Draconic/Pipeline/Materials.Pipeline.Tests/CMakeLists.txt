# Draconic::Materials.Pipeline unit tests (doctest; content DB - no GPU/DXC).

add_executable(Draconic.Materials.Pipeline.Tests
    TestsMain.cpp
    MaterialEditorTests.cpp)

target_link_libraries(Draconic.Materials.Pipeline.Tests PRIVATE
    Draconic::Materials.Pipeline Draconic::VFS doctest::doctest)

target_compile_definitions(Draconic.Materials.Pipeline.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Materials.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Materials.Pipeline.Tests COMMAND Draconic.Materials.Pipeline.Tests)
