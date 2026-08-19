# The tests REGISTER a backend (an entry point's job) - the provider under test stays backend-neutral
# and resolves through the registry, exactly like the ScriptPage. Also home to the per-language
# editor-UI tests (CodeEditView lexer registration): editor-side, UI.Toolkit-linked, so they belong
# here and NOT in the (UI-free) Pipeline cook test projects.
# Each backend's editor-UI test (CodeEditView lexer registration) imports that backend's
# editor.script.<lang> module, so it compiles only when the backend is on.
set(_editor_script_backend_ui_tests "")
if(OPTION_ENABLE_ANGELSCRIPT)
    list(APPEND _editor_script_backend_ui_tests AngelScriptEditorUITests.cpp)
endif()
if(OPTION_ENABLE_LUAU)
    list(APPEND _editor_script_backend_ui_tests LuauEditorUITests.cpp)
endif()
add_executable(Editor.Script.Tests TestsMain.cpp ScriptCompletionTests.cpp ScriptApiBrowserTests.cpp
    ${_editor_script_backend_ui_tests})
target_link_libraries(Editor.Script.Tests PRIVATE
    Editor::Script ${SCRIPTLIB_ANGELSCRIPT_EDITOR} ${SCRIPTLIB_LUAU_EDITOR}
    ${SCRIPTLIB_ANGELSCRIPT} ${SCRIPTLIB_LUAU} Foundation::Script Foundation::Script.Facades
    Foundation::UI.Toolkit Foundation::UI Foundation::Core
    doctest::doctest)
target_compile_definitions(Editor.Script.Tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_compile_options(Editor.Script.Tests PRIVATE
    "$<$<CXX_COMPILER_ID:Clang>:-Wno-error=#warnings;-Wno-error=c2y-extensions>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=cpp>")
add_test(NAME Editor.Script.Tests COMMAND Editor.Script.Tests)
