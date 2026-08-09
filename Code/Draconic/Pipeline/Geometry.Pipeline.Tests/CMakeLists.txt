# Draconic::Geometry.Pipeline unit tests (doctest; content DB - no GPU).

add_executable(Draconic.Geometry.Pipeline.Tests
    TestsMain.cpp
    MeshEditorTests.cpp)

target_link_libraries(Draconic.Geometry.Pipeline.Tests PRIVATE
    Draconic::Geometry.Pipeline Draconic::VFS doctest::doctest)

target_compile_definitions(Draconic.Geometry.Pipeline.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.Geometry.Pipeline.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.Geometry.Pipeline.Tests COMMAND Draconic.Geometry.Pipeline.Tests)
