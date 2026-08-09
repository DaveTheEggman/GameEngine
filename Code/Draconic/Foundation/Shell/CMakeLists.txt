# Foundation::Shell - `draconic.shell` (IShell/IWindow, the raw OS/windowing service - what
# Sedulous calls the "shell"). Interfaces only; native + null backends implement them.

add_library(Shell STATIC)
add_library(Foundation::Shell ALIAS Shell)

target_link_libraries(Shell PUBLIC Foundation::Core Foundation::Policy)

target_sources(Shell
    PUBLIC
        FILE_SET shell_modules TYPE CXX_MODULES FILES
            InputTypes.cppm
            Input.cppm
            InputSurface.cppm
            Dialog.cppm
            ShellModule.cppm)
