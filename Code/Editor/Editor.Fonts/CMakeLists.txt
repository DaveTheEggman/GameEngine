# Editor::Fonts - `editor.fonts` (the FontAsset authoring page:
# live atlas bake preview + bake-intent property grid). Tooling only.

util_add_engine_library(Editor.Fonts ALIAS Editor::Fonts)
target_link_libraries(Editor.Fonts
    PUBLIC Foundation::Core Foundation::Content Foundation::VFS Foundation::Image
           Foundation::Fonts Foundation::Fonts.TTF Foundation::Fonts.IO Foundation::Fonts.Importer
           Foundation::Fonts.DistanceField.Baker Pipeline::Fonts
           Foundation::UI Foundation::UI.Toolkit Editor::Core Editor::App
           Foundation::Policy)

target_sources(Editor.Fonts
    PRIVATE
        FontEditorPageImpl.cpp
    PUBLIC
        FILE_SET editor_fonts_modules TYPE CXX_MODULES FILES
            FontEditorPage.cppm FontThumbnailGenerator.cppm)
