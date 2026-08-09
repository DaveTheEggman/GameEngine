# Editor::Generic - `editor.generic`: the serialize-driven FALLBACK asset
# page (every asset without a bespoke page). Linked by the editor EXECUTABLE only.
add_library(Editor.Generic STATIC)
add_library(Editor::Generic ALIAS Editor.Generic)

target_link_libraries(Editor.Generic
    PUBLIC
        Foundation::Core Foundation::Content
        Foundation::UI Foundation::UI.Toolkit
        Editor::Core Editor::App Foundation::Policy)

target_sources(Editor.Generic
    PRIVATE
        AssetFormPageImpl.cpp
    PUBLIC
        FILE_SET editorgeneric_modules TYPE CXX_MODULES FILES
            AssetFormPage.cppm)

