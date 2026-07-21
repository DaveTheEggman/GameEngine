
add_executable(DraconicNetSubsystemTests TestsMain.cpp SubsystemTests.cpp FacadeScriptTests.cpp)
target_link_libraries(DraconicNetSubsystemTests PRIVATE Draconic::NetSubsystem
    Draconic::ScriptWren Draconic::ScriptAngelScript doctest::doctest)
target_compile_definitions(DraconicNetSubsystemTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicNetSubsystemTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicNetSubsystemTests COMMAND DraconicNetSubsystemTests)
