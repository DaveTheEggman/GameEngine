
add_executable(Net.Replication.Tests TestsMain.cpp ReplicationTests.cpp)
target_link_libraries(Net.Replication.Tests PRIVATE Foundation::Net.Replication Foundation::Scene doctest::doctest)
target_compile_definitions(Net.Replication.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Net.Replication.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Net.Replication.Tests COMMAND Net.Replication.Tests)
