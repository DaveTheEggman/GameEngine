# Experimental::GUI.Shell - `experimental.gui.shell`, the platform input bridge: translates
# foundation.shell InputEvents into GUI EventDispatcher injections. The ONLY GUI module that
# depends on the platform input layer, keeping experimental.gui itself platform-agnostic.
add_library(GUI.Shell STATIC)
add_library(Experimental::GUI.Shell ALIAS GUI.Shell)

target_link_libraries(GUI.Shell
    PUBLIC Experimental::GUI Foundation::Shell Foundation::Core Foundation::Policy)

target_sources(GUI.Shell
    PUBLIC
        FILE_SET guishell_modules TYPE CXX_MODULES FILES
            GuiInputBridge.cppm)

