
add_executable(Draconic.Engine.GameInstance.Tests TestsMain.cpp GameInstanceTests.cpp)
target_link_libraries(Draconic.Engine.GameInstance.Tests PRIVATE
    Draconic::RuntimeGameInstance Draconic::Net Draconic::NetManager
    Draconic::Input Draconic::Shell Draconic::ScriptWren Draconic::ScriptAngelScript doctest::doctest)
target_compile_definitions(Draconic.Engine.GameInstance.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Draconic.Engine.GameInstance.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings>" "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Draconic.Engine.GameInstance.Tests COMMAND Draconic.Engine.GameInstance.Tests)
