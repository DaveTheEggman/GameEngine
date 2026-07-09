# Draconic::UIShell - `draconic.ui.shell`: the platform input bridge for draconic.ui.
# Reimplemented (not ported from Sedulous.UI.Shell): translates draconic.shell input
# events / an InputSurface into UIContext InputManager calls and drives window text
# input (IME) from UI focus. Kept separate so the core draconic.ui stays platform-free.
add_library(DraconicUIShell STATIC)
add_library(Draconic::UIShell ALIAS DraconicUIShell)

target_link_libraries(DraconicUIShell
    PUBLIC Draconic::Core Draconic::UI Draconic::Shell Draconic::Policy)

target_sources(DraconicUIShell
    PUBLIC
        FILE_SET uishell_modules TYPE CXX_MODULES FILES
            UiInputBridge.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
