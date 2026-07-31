# Draconic::GUIVFS - `draconic.gui.vfs`: a VFS-backed IResourceProvider for the GUI's CSS
# resource seam (background-image: url()). Kept separate from the core Draconic::GUI so the GUI
# stays free of a VFS dependency (the app links this and hands the provider to a StyleManager).
# Mirrors Draconic::UIVFS / draconic.gui.shell.
add_library(Draconic.GUI.VFS STATIC)
add_library(Draconic::GUIVFS ALIAS Draconic.GUI.VFS)

target_link_libraries(Draconic.GUI.VFS
    PUBLIC Draconic::Core Draconic::GUI Draconic::Image Draconic::ImageIO Draconic::VFS Draconic::Policy)

target_sources(Draconic.GUI.VFS
    PUBLIC
        FILE_SET guivfs_modules TYPE CXX_MODULES FILES
            VfsResourceProvider.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.GUI.VFS.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
