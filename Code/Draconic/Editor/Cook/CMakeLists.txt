# Draconic::EditorCook - `draconic.editor.cook` (the incremental cook driver + pipeline DB).
# Tooling only; headless (no shell/GPU) so the CLI cooker can link it.

add_library(DraconicEditorCook STATIC)
add_library(Draconic::EditorCook ALIAS DraconicEditorCook)

target_link_libraries(DraconicEditorCook PUBLIC
    Draconic::Core Draconic::Content Draconic::VFS Draconic::Editor Draconic::Policy)

target_sources(DraconicEditorCook
    PUBLIC
        FILE_SET editorcook_modules TYPE CXX_MODULES FILES
            CookDriver.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
