add_executable(Draconic.GUI.VFS.Tests
    VfsResourceProviderTests.cpp
    TestsMain.cpp)

target_link_libraries(Draconic.GUI.VFS.Tests PRIVATE
    Draconic::GUI.VFS Draconic::VFS Draconic::Image Draconic::Image.IO doctest::doctest)

target_compile_definitions(Draconic.GUI.VFS.Tests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(Draconic.GUI.VFS.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME Draconic.GUI.VFS.Tests COMMAND Draconic.GUI.VFS.Tests)
