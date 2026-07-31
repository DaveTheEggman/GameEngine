# Draconic::EditorApp - `draconic.editor.app`: the editor UI shell (chrome + layout persistence +
# EditorApplication) on draconic.ui / toolkit / the runtime docking host. Tooling only.

add_library(Draconic.Editor.App STATIC)
add_library(Draconic::EditorApp ALIAS Draconic.Editor.App)

target_link_libraries(Draconic.Editor.App
    PUBLIC
        Draconic::Core Draconic::VFS Draconic::XmlSerialization Draconic::Editor Draconic::Resource
        Draconic::Shell Draconic::Graphics Draconic::Runtime Draconic::RuntimeClient Draconic::RuntimeDefaultApp
        Draconic::RenderApi
        Draconic::Fonts Draconic::FontsTTF Draconic::FontsDFBaker
        Draconic::UI Draconic::UIToolkit Draconic::UIRuntime Draconic::UIApplication
        Draconic::UIResource Draconic::UISubsystem
        Draconic::Input Draconic::InputSubsystem
        Draconic::EditorCore Draconic::Settings Draconic::Policy)

target_sources(Draconic.Editor.App
    PRIVATE
        ApplicationImpl.cpp
        AssetsViewImpl.cpp
        AssetPickerDialogImpl.cpp
        SettingsDialogImpl.cpp
        EditorIconsImpl.cpp
    PUBLIC
        FILE_SET editorapp_modules TYPE CXX_MODULES FILES
            Layout.cppm
            LogView.cppm
            UIPage.cppm
            AssetsView.cppm
            AssetPickerDialog.cppm
            ImportDialog.cppm
            SettingsDialog.cppm
            PreferencesDialog.cppm
            EditorIcons.cppm
            Shell.cppm
            Application.cppm
            EditorAppModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.App.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
