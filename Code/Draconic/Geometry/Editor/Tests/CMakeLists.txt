# Draconic::MeshEditor unit tests (doctest; content DB - no GPU).

add_executable(DraconicMeshEditorTests
    TestsMain.cpp
    MeshEditorTests.cpp)

target_link_libraries(DraconicMeshEditorTests PRIVATE
    Draconic::MeshEditor Draconic::VFS doctest::doctest)

target_compile_definitions(DraconicMeshEditorTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicMeshEditorTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicMeshEditorTests COMMAND DraconicMeshEditorTests)
