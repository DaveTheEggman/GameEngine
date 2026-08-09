# Draconic::Pipeline.Cook - `draconic.editor.cook` (the incremental cook driver + pipeline DB).
# Tooling only; headless (no shell/GPU) so the CLI cooker can link it.

add_library(Draconic.Pipeline.Cook STATIC)
add_library(Draconic::Pipeline.Cook ALIAS Draconic.Pipeline.Cook)

target_link_libraries(Draconic.Pipeline.Cook PUBLIC
    Draconic::Core Draconic::Content Draconic::VFS Draconic::Pipeline.Core Draconic::Policy)

target_sources(Draconic.Pipeline.Cook
    PRIVATE
        CookDriverImpl.cpp
    PUBLIC
        FILE_SET editorcook_modules TYPE CXX_MODULES FILES
            CookDriver.cppm)

