# Foundation::VFS unit tests (doctest).

add_executable(VFS.Tests TestsMain.cpp VfsTests.cpp)
target_link_libraries(VFS.Tests PRIVATE Foundation::VFS doctest::doctest)
target_compile_definitions(VFS.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(VFS.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME VFS.Tests COMMAND VFS.Tests)
