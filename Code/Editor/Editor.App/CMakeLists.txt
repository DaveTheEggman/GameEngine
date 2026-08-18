# Editor::App - `editor.app`: the editor UI shell (chrome + layout persistence +
# EditorApplication) on foundation.ui / toolkit / the runtime docking host. Tooling only.

add_library(Editor.App STATIC)
add_library(Editor::App ALIAS Editor.App)

target_link_libraries(Editor.App
    PUBLIC
        Foundation::Core Foundation::VFS Foundation::Xml.Serialization Pipeline::Core Foundation::Resource
        Foundation::Shell Foundation::Graphics Foundation::Runtime Foundation::Runtime.Client Engine::DefaultApp
        Foundation::Render.Api
        Foundation::Fonts Foundation::Fonts.TTF Foundation::Fonts.Resource Foundation::Fonts.DistanceField.Baker
        Foundation::UI Foundation::UI.Toolkit Foundation::UI.Runtime Foundation::UI.Application
        Foundation::UI.Resource Engine::UI
        Foundation::Input Engine::Input
        Editor::Core Editor::ViewportTools Foundation::Settings Foundation::Policy)

target_sources(Editor.App
    PRIVATE
        ApplicationImpl.cpp
        AssetsViewImpl.cpp
        AssetPickerDialogImpl.cpp
        SettingsDialogImpl.cpp
        EditorIconsImpl.cpp
        ToolPanelImpl.cpp
    PUBLIC
        FILE_SET editorapp_modules TYPE CXX_MODULES FILES
            Layout.cppm
            LogView.cppm
            UIPage.cppm
            PageToolbar.cppm
            ToolPanel.cppm
            AssetsView.cppm
            AssetPickerDialog.cppm
            AssetCreateDialog.cppm
            ConfirmDialog.cppm
            PathPickerDialog.cppm
            GroupPickerDialog.cppm
            ImportDialog.cppm
            SettingsDialog.cppm
            PreferencesDialog.cppm
            EditorIcons.cppm
            AssetPickerSlot.cppm
            AssetDragData.cppm
            ProjectManagerView.cppm
            Shell.cppm
            Application.cppm
            EditorAppModule.cppm)

