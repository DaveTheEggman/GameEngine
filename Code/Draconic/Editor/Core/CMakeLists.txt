# Draconic::EditorCore - `draconic.editor.core` (headless editor domain: context, registries,
# command stack, pages, selection, project). Tooling only - the runtime never links this.

add_library(DraconicEditorCore STATIC)
add_library(Draconic::EditorCore ALIAS DraconicEditorCore)

target_link_libraries(DraconicEditorCore
    PUBLIC Draconic::Core Draconic::VFS Draconic::VFSPak Draconic::Content Draconic::XmlSerialization Draconic::Project
           Draconic::Settings Draconic::SceneResource
           Draconic::Editor Draconic::EditorCook Draconic::Resource Draconic::Policy)

target_sources(DraconicEditorCore
    PRIVATE
        CookServiceImpl.cpp
        ContextImpl.cpp
        CommandImpl.cpp
        ImporterImpl.cpp
        ExportTemplateImpl.cpp
        ExportImpl.cpp
    PUBLIC
        FILE_SET editorcore_modules TYPE CXX_MODULES FILES
            Command.cppm
            Selection.cppm
            Project.cppm
            Export.cppm
            Page.cppm
            Context.cppm
            LogBuffer.cppm
            JobService.cppm
            ExportPreset.cppm
            ExportRoots.cppm
            ExportTemplate.cppm
            ExportController.cppm
            CookService.cppm
            Importer.cppm
            EditorCoreModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
