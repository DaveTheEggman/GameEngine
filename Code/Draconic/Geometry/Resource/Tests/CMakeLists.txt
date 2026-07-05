# Draconic::MeshResource unit tests (doctest; content DB - no GPU).

add_executable(DraconicMeshResourceTests
    TestsMain.cpp
    MeshResourceTests.cpp)

target_link_libraries(DraconicMeshResourceTests PRIVATE
    Draconic::MeshResource Draconic::VFS doctest::doctest)

target_compile_definitions(DraconicMeshResourceTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicMeshResourceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicMeshResourceTests COMMAND DraconicMeshResourceTests)
