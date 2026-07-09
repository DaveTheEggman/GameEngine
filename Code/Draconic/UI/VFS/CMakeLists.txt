# Draconic::UIVFS - `draconic.ui.vfs`: a VFS-backed IResourceProvider for the UI.
# Kept separate from the core Draconic::UI so the UI stays free of a VFS dependency
# (the app links this and wires the provider into StyleSheetLoader). Mirrors
# Sedulous.UI.IO.
add_library(DraconicUIVFS STATIC)
add_library(Draconic::UIVFS ALIAS DraconicUIVFS)

target_link_libraries(DraconicUIVFS
    PUBLIC Draconic::Core Draconic::UI Draconic::Image Draconic::ImageIO Draconic::VFS Draconic::Policy)

target_sources(DraconicUIVFS
    PUBLIC
        FILE_SET uivfs_modules TYPE CXX_MODULES FILES
            VfsResourceProvider.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
