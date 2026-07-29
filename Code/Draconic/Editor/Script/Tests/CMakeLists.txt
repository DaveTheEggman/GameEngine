# The tests REGISTER the Wren backend (an entry point's job) - the provider under test stays
# backend-neutral and resolves through the registry, exactly like the ScriptPage.
add_executable(DraconicEditorScriptTests TestsMain.cpp ScriptCompletionTests.cpp)
target_link_libraries(DraconicEditorScriptTests PRIVATE
    Draconic::EditorScript Draconic::ScriptWren Draconic::Script Draconic::ScriptFacades
    Draconic::UIToolkit Draconic::UI Draconic::Core
    doctest::doctest)
target_compile_definitions(DraconicEditorScriptTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(DraconicEditorScriptTests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME DraconicEditorScriptTests COMMAND DraconicEditorScriptTests)
