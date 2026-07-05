# Draconic::RuntimeScript unit tests (doctest). Uses the Wren backend as the
# injected script manager.

add_executable(DraconicRuntimeScriptTests
    TestsMain.cpp
    ScriptSubsystemTests.cpp)

target_link_libraries(DraconicRuntimeScriptTests PRIVATE
    Draconic::RuntimeScript Draconic::ScriptWren doctest::doctest)

target_compile_definitions(DraconicRuntimeScriptTests PRIVATE
    DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

target_compile_options(DraconicRuntimeScriptTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")

add_test(NAME DraconicRuntimeScriptTests COMMAND DraconicRuntimeScriptTests)
