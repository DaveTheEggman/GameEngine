
add_executable(DraconicNetManagerTests TestsMain.cpp ManagerTests.cpp FacadeScriptTests.cpp ReplicationWiringTests.cpp)
target_link_libraries(DraconicNetManagerTests PRIVATE Draconic::NetManager
    Draconic::ScriptWren Draconic::ScriptAngelScript doctest::doctest)
target_compile_definitions(DraconicNetManagerTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicNetManagerTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicNetManagerTests COMMAND DraconicNetManagerTests)
