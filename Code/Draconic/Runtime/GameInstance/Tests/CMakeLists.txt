
add_executable(DraconicRuntimeGameInstanceTests TestsMain.cpp GameInstanceTests.cpp)
target_link_libraries(DraconicRuntimeGameInstanceTests PRIVATE
    Draconic::RuntimeGameInstance Draconic::Net Draconic::NetManager
    Draconic::Input Draconic::Shell Draconic::ScriptWren Draconic::ScriptAngelScript doctest::doctest)
target_compile_definitions(DraconicRuntimeGameInstanceTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicRuntimeGameInstanceTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicRuntimeGameInstanceTests COMMAND DraconicRuntimeGameInstanceTests)
