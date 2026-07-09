add_executable(DraconicUIVFSTests
    VfsResourceProviderTests.cpp
    TestsMain.cpp)

target_link_libraries(DraconicUIVFSTests PRIVATE
    Draconic::UIVFS Draconic::VFS doctest::doctest)

target_compile_definitions(DraconicUIVFSTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicUIVFSTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicUIVFSTests COMMAND DraconicUIVFSTests)
