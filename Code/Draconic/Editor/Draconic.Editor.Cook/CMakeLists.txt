# Draconic::EditorCook - `draconic.editor.cook` (the incremental cook driver + pipeline DB).
# Tooling only; headless (no shell/GPU) so the CLI cooker can link it.

add_library(Draconic.Editor.Cook STATIC)
add_library(Draconic::EditorCook ALIAS Draconic.Editor.Cook)

target_link_libraries(Draconic.Editor.Cook PUBLIC
    Draconic::Core Draconic::Content Draconic::VFS Draconic::Editor Draconic::Policy)

target_sources(Draconic.Editor.Cook
    PRIVATE
        CookDriverImpl.cpp
    PUBLIC
        FILE_SET editorcook_modules TYPE CXX_MODULES FILES
            CookDriver.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.Cook.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
