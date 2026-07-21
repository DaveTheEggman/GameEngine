
add_executable(DraconicNetReplicationTests TestsMain.cpp ReplicationTests.cpp)
target_link_libraries(DraconicNetReplicationTests PRIVATE Draconic::NetReplication doctest::doctest)
target_compile_definitions(DraconicNetReplicationTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicNetReplicationTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicNetReplicationTests COMMAND DraconicNetReplicationTests)
