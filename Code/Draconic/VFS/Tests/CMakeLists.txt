# Draconic::VFS unit tests (doctest).

add_executable(DraconicVFSTests TestsMain.cpp VfsTests.cpp)
target_link_libraries(DraconicVFSTests PRIVATE Draconic::VFS doctest::doctest)
target_compile_definitions(DraconicVFSTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicVFSTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicVFSTests COMMAND DraconicVFSTests)
