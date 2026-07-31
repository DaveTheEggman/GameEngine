# The tests REGISTER the AngelScript cook (an entry point's job) - the library under test
# stays backend-neutral and resolves through the registry (B3).
add_executable(DraconicAngelScriptCookTests TestsMain.cpp AngelScriptCookTests.cpp
    AngelScriptEditorUITests.cpp)
target_link_libraries(DraconicAngelScriptCookTests PRIVATE
    Draconic::Script.AngelScript.Editor Draconic::Script.AngelScript.Editor.UI
    Draconic::Script.AngelScript Draconic::Script.Editor
    Draconic::Script.Resource Draconic::Script Draconic::Script.Facades
    Draconic::UI.Toolkit Draconic::UI
    doctest::doctest)
target_compile_definitions(DraconicAngelScriptCookTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicAngelScriptCookTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicAngelScriptCookTests COMMAND DraconicAngelScriptCookTests)
