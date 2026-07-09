# Draconic::GUIShell - `draconic.gui.shell`, the platform input bridge: translates
# draconic.shell InputEvents into GUI EventDispatcher injections. The ONLY GUI module that
# depends on the platform input layer, keeping draconic.gui itself platform-agnostic.
add_library(DraconicGUIShell STATIC)
add_library(Draconic::GUIShell ALIAS DraconicGUIShell)

target_link_libraries(DraconicGUIShell
    PUBLIC Draconic::GUI Draconic::Shell Draconic::Core Draconic::Policy)

target_sources(DraconicGUIShell
    PUBLIC
        FILE_SET guishell_modules TYPE CXX_MODULES FILES
            GuiInputBridge.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
