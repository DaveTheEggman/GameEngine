# Editor::PropertyAnimation - `editor.propertyanimation`: the persistent in-scene PropertyAnimationPanel
# (host + preview + Timeline scrubber over the shared ClipEditorView) + the New Asset creator. Linked by
# the editor EXECUTABLE only.
util_add_engine_library(Editor.PropertyAnimation ALIAS Editor::PropertyAnimation)
target_link_libraries(Editor.PropertyAnimation
    PUBLIC
        Foundation::Core Foundation::Content Foundation::Scene Foundation::Render
        Foundation::Runtime Foundation::Runtime.Client
        Foundation::PropertyAnimation Foundation::PropertyAnimation.Resource Pipeline::PropertyAnimation
        Foundation::UI Foundation::UI.Runtime Foundation::UI.Toolkit
        Editor::Core Editor::App Foundation::Policy)

target_sources(Editor.PropertyAnimation
    PRIVATE
        ClipEditorViewImpl.cpp
        PropertyAnimationPanelImpl.cpp
    PUBLIC
        FILE_SET editorpropanim_modules TYPE CXX_MODULES FILES
            ClipEditorView.cppm
            PropertyAnimationPanel.cppm
            PropertyAnimationEditorModule.cppm)
