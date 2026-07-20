# Draconic::EditorScript - `draconic.editor.script`: the ScriptPage (behavior source text
# editor + save->recook->hot-reload + inline compile errors). Editor exe only.
add_library(DraconicEditorScript STATIC)
add_library(Draconic::EditorScript ALIAS DraconicEditorScript)
target_link_libraries(DraconicEditorScript PUBLIC
    Draconic::Core Draconic::Content Draconic::RuntimeClient Draconic::UI
    Draconic::Script Draconic::ScriptEditor Draconic::ScriptResource
    Draconic::EditorCore Draconic::EditorApp Draconic::Policy)
target_sources(DraconicEditorScript PUBLIC FILE_SET editor_script_modules TYPE CXX_MODULES FILES ScriptPage.cppm)

# The headless editing seam (ScriptSourceDocument, in draconic.script.editor) is covered by the
# script pipeline tests; the UI page itself is exercised by the editor smoke.
