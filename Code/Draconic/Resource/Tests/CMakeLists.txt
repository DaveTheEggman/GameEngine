# Draconic::Resource unit tests (doctest).

add_executable(DraconicResourceTests TestsMain.cpp ResourceTests.cpp)
target_link_libraries(DraconicResourceTests PRIVATE Draconic::Resource Draconic::VFS doctest::doctest)
target_compile_definitions(DraconicResourceTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicResourceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicResourceTests COMMAND DraconicResourceTests)
