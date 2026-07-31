# Draconic::Materials.Editor unit tests (doctest; content DB - no GPU/DXC).

add_executable(Draconic.Materials.Editor.Tests
    TestsMain.cpp
    MaterialEditorTests.cpp)

target_link_libraries(Draconic.Materials.Editor.Tests PRIVATE
    Draconic::Materials.Editor Draconic::VFS doctest::doctest)

target_compile_definitions(Draconic.Materials.Editor.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Materials.Editor.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Materials.Editor.Tests COMMAND Draconic.Materials.Editor.Tests)
