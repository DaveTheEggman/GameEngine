# Foundation::UI.Shell - `draconic.ui.shell`: the platform input bridge for draconic.ui.
# Reimplemented (not ported from Sedulous.UI.Shell): translates draconic.shell input
# events / an InputSurface into UIContext InputManager calls and drives window text
# input (IME) from UI focus. Kept separate so the core draconic.ui stays platform-free.
add_library(UI.Shell STATIC)
add_library(Foundation::UI.Shell ALIAS UI.Shell)

target_link_libraries(UI.Shell
    PUBLIC Foundation::Core Foundation::UI Foundation::Shell Foundation::Policy)

target_sources(UI.Shell
    PUBLIC
        FILE_SET uishell_modules TYPE CXX_MODULES FILES
            UiInputBridge.cppm)

