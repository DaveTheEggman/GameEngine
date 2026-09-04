# Experimental::GUI.VFS - `experimental.gui.vfs`: a VFS-backed IResourceProvider for the GUI's CSS
# resource seam (background-image: url()). Kept separate from the core Experimental::GUI so the GUI
# stays free of a VFS dependency (the app links this and hands the provider to a StyleManager).
# Mirrors Foundation::UI.VFS / experimental.gui.shell.
util_add_engine_library(GUI.VFS ALIAS Experimental::GUI.VFS)
target_link_libraries(GUI.VFS
    PUBLIC Foundation::Core Experimental::GUI Foundation::Image Foundation::Image.IO Foundation::VFS Foundation::Policy)

target_sources(GUI.VFS
    PUBLIC
        FILE_SET guivfs_modules TYPE CXX_MODULES FILES
            VfsResourceProvider.cppm)

