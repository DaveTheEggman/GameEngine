# Draconic::VFSPak unit tests (doctest).

add_executable(DraconicVFSPakTests TestsMain.cpp PakTests.cpp)
target_link_libraries(DraconicVFSPakTests PRIVATE Draconic::VFSPak doctest::doctest)
target_compile_definitions(DraconicVFSPakTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicVFSPakTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicVFSPakTests COMMAND DraconicVFSPakTests)
