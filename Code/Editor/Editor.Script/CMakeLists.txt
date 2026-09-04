# Editor::Script - `editor.script`: the ScriptPage (behavior source text
# editor + save->recook->hot-reload + inline compile errors). Editor exe only.
util_add_engine_library(Editor.Script ALIAS Editor::Script)
target_link_libraries(Editor.Script PUBLIC
    Foundation::Core Foundation::Content Foundation::Runtime.Client Foundation::UI Foundation::UI.Toolkit
    Foundation::Script Foundation::Script.Facades Pipeline::Script Foundation::Script.Resource
    Editor::Core Editor::App Foundation::Policy)
target_sources(Editor.Script PRIVATE ScriptPageImpl.cpp ScriptCompletionImpl.cpp ScriptApiSurfaceImpl.cpp ScriptApiBrowserImpl.cpp PUBLIC FILE_SET editor_script_modules TYPE CXX_MODULES FILES ScriptPage.cppm)

# The headless editing seam (ScriptSourceDocument, in script.pipeline) is covered by the
# script pipeline tests; the UI page itself is exercised by the editor smoke.

