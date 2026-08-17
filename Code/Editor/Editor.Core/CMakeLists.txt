# Editor::Core - `editor.core` (headless editor domain: context, registries,
# command stack, pages, selection, project). Tooling only - the runtime never links this.

add_library(Editor.Core STATIC)
add_library(Editor::Core ALIAS Editor.Core)

target_link_libraries(Editor.Core
    PUBLIC Pipeline::Importer Foundation::Core Foundation::VFS Foundation::VFS.Pak Foundation::Content Foundation::Xml.Serialization Engine::Project
           Foundation::Settings Foundation::Scene.Resource
           Pipeline::Cook Foundation::Resource Foundation::Policy
           Foundation::Image Foundation::Image.IO Foundation::UI
           Foundation::Shaders) # export cooks the engine shader pack (shaders.dpak)

# Export cooks the built-in shaders from the source tree (same root the renderer bakes); a
# relocated editor falls back to a "Shaders" dir beside it.
if(TARGET Foundation::Shaders)
    target_compile_definitions(Editor.Core PRIVATE
        BUILTIN_ENGINE_SHADER_DIR="${CMAKE_SOURCE_DIR}/Data/Shaders")
endif()

# The player executable basename, from the build's single source of truth (root CMakeLists). The
# export host-template stages "<base>[.exe]" and reads "<base>.runtime-libs" beside the running tool.
target_compile_definitions(Editor.Core PRIVATE
    BUILTIN_PLAYER_BASENAME=u8"${BUILTIN_PLAYER_BASENAME}")

target_sources(Editor.Core
    PRIVATE
        CookServiceImpl.cpp
        ThumbnailServiceImpl.cpp
        ContextImpl.cpp
        CommandImpl.cpp
        ExportTemplateImpl.cpp
        ExportImpl.cpp
    PUBLIC
        FILE_SET editorcore_modules TYPE CXX_MODULES FILES
            Command.cppm
            Selection.cppm
            Project.cppm
            ProjectRegistry.cppm
            ProjectManager.cppm
            Export.cppm
            Page.cppm
            Context.cppm
            LogBuffer.cppm
            JobService.cppm
            ThumbnailService.cppm
            ExportPreset.cppm
            EditorSettings.cppm
            ExportRoots.cppm
            ExportTemplate.cppm
            ExportController.cppm
            CookService.cppm
            EditorCoreModule.cppm)

