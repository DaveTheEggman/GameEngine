# Draconic::ShellWeb - `draconic.shell.web` (a single-canvas IShell over Emscripten's HTML5 API).
# The web counterpart of the SDL3 desktop backend; Emscripten-only. Self-contained - it does not
# borrow the Null shell's stubs, so the HTML5 input/dialog wiring has a home here. One partition per
# component: window, window manager, input devices, dialogs, and the composing shell.
add_library(DraconicShellWeb STATIC)
add_library(Draconic::ShellWeb ALIAS DraconicShellWeb)

target_link_libraries(DraconicShellWeb PUBLIC Draconic::Shell Draconic::Policy)

target_sources(DraconicShellWeb
    PUBLIC
        FILE_SET shell_web_modules TYPE CXX_MODULES FILES
            WebWindow.cppm
            WebWindowManager.cppm
            WebInput.cppm
            WebDialog.cppm
            WebShell.cppm)
