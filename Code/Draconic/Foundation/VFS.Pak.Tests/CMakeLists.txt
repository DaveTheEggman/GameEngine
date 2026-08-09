# Foundation::VFS.Pak unit tests (doctest).

add_executable(VFS.Pak.Tests TestsMain.cpp PakTests.cpp)
target_link_libraries(VFS.Pak.Tests PRIVATE Foundation::VFS.Pak doctest::doctest)
target_compile_definitions(VFS.Pak.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(VFS.Pak.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME VFS.Pak.Tests COMMAND VFS.Pak.Tests)
