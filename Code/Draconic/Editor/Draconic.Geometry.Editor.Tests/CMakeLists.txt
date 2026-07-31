# Draconic::MeshEditor unit tests (doctest; content DB - no GPU).

add_executable(Draconic.Geometry.Editor.Tests
    TestsMain.cpp
    MeshEditorTests.cpp)

target_link_libraries(Draconic.Geometry.Editor.Tests PRIVATE
    Draconic::MeshEditor Draconic::VFS doctest::doctest)

target_compile_definitions(Draconic.Geometry.Editor.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Geometry.Editor.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Geometry.Editor.Tests COMMAND Draconic.Geometry.Editor.Tests)
