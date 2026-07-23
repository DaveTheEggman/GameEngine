# Draconic::EditorInput - `draconic.editor.input`: the InputMapAsset editing page + factory.
# Linked by the editor EXECUTABLE only.
add_library(DraconicEditorInput STATIC)
add_library(Draconic::EditorInput ALIAS DraconicEditorInput)

target_link_libraries(DraconicEditorInput
    PUBLIC
        Draconic::Core Draconic::Content Draconic::Shell
        Draconic::Runtime Draconic::RuntimeClient
        Draconic::Input Draconic::InputResource Draconic::InputEditor
        Draconic::UI Draconic::UIRuntime Draconic::UIToolkit
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(DraconicEditorInput
    PRIVATE
        InputMapPageImpl.cpp
    PUBLIC
        FILE_SET editorinput_modules TYPE CXX_MODULES FILES
            InputMapPage.cppm)
