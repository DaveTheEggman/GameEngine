# Editor::Input - `editor.input`: the InputMapAsset editing page + factory.
# Linked by the editor EXECUTABLE only.
add_library(Editor.Input STATIC)
add_library(Editor::Input ALIAS Editor.Input)

target_link_libraries(Editor.Input
    PUBLIC
        Foundation::Core Foundation::Content Foundation::Shell
        Foundation::Runtime Foundation::Runtime.Client
        Foundation::Input Foundation::Input.Resource Pipeline::Input
        Foundation::UI Foundation::UI.Runtime Foundation::UI.Toolkit
        Editor::Core Editor::App Foundation::Policy)

target_sources(Editor.Input
    PRIVATE
        InputMapPageImpl.cpp
    PUBLIC
        FILE_SET editorinput_modules TYPE CXX_MODULES FILES
            InputMapPage.cppm)
