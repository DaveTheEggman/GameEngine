# The tests REGISTER the Wren backend (an entry point's job) - the provider under test stays
# backend-neutral and resolves through the registry, exactly like the ScriptPage. Also home to the
# per-language editor-UI tests (CodeEditView lexer registration): editor-side, UI.Toolkit-linked,
# so they belong here and NOT in the (UI-free) Pipeline cook test projects.
add_executable(Editor.Script.Tests TestsMain.cpp ScriptCompletionTests.cpp ScriptApiBrowserTests.cpp
    WrenEditorUITests.cpp AngelScriptEditorUITests.cpp LuauEditorUITests.cpp)
target_link_libraries(Editor.Script.Tests PRIVATE
    Editor::Script Editor::Script.Wren Editor::Script.AngelScript Editor::Script.Luau
    Foundation::Script.Wren Foundation::Script Foundation::Script.Facades
    Foundation::UI.Toolkit Foundation::UI Foundation::Core
    doctest::doctest)
target_compile_definitions(Editor.Script.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Script.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Script.Tests COMMAND Editor.Script.Tests)
