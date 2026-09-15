# Editor::Core - `editor.core` (headless editor domain: context, registries,
# command stack, pages, selection, project). Tooling only - the runtime never links this.

util_add_engine_library(Editor.Core ALIAS Editor::Core)
target_link_libraries(Editor.Core
    PUBLIC Pipeline::Importer Foundation::Core Foundation::VFS Foundation::VFS.Pak Foundation::Content Foundation::Xml.Serialization Engine::Project
           Foundation::Settings Foundation::Scene.Resource
           Pipeline::Cook Foundation::Resource Foundation::Policy
           Foundation::Image Foundation::Image.IO Foundation::UI
           Foundation::Shaders Foundation::Shaders.System) # export cooks the engine shader pack (Data/Shaders/shaders.dpak)

# The player executable basename, from the build's single source of truth (root CMakeLists). The
# export host-template stages "<base>[.exe]" and reads "<base>.runtime-libs" beside the running tool.
target_compile_definitions(Editor.Core PRIVATE
    BUILTIN_PLAYER_BASENAME=u8"${BUILTIN_PLAYER_BASENAME}")

# Native ship link (game-native-code.md N3): the exporter re-invokes THIS cmake over THIS
# checkout to build Engine.GamePlayer with the project's native module statically linked.
# Both baked like the shader dir above; a relocated editor reports clearly instead of guessing.
target_compile_definitions(Editor.Core PRIVATE
    BUILDSYSTEM_CMAKE_PATH="${CMAKE_COMMAND}"
    BUILDSYSTEM_ENGINE_ROOT="${CMAKE_SOURCE_DIR}"
    BUILDSYSTEM_CXX_COMPILER="${CMAKE_CXX_COMPILER}")

target_sources(Editor.Core
    PRIVATE
        CookServiceImpl.cpp
        ThumbnailServiceImpl.cpp
        ContextImpl.cpp
        ProjectScaffoldImpl.cpp
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

