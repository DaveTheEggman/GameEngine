# Draconic::GUIVFS - `draconic.gui.vfs`: a VFS-backed IResourceProvider for the GUI's CSS
# resource seam (background-image: url()). Kept separate from the core Draconic::GUI so the GUI
# stays free of a VFS dependency (the app links this and hands the provider to a StyleManager).
# Mirrors Draconic::UIVFS / draconic.gui.shell.
add_library(DraconicGUIVFS STATIC)
add_library(Draconic::GUIVFS ALIAS DraconicGUIVFS)

target_link_libraries(DraconicGUIVFS
    PUBLIC Draconic::Core Draconic::GUI Draconic::Image Draconic::ImageIO Draconic::VFS Draconic::Policy)

target_sources(DraconicGUIVFS
    PUBLIC
        FILE_SET guivfs_modules TYPE CXX_MODULES FILES
            VfsResourceProvider.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
