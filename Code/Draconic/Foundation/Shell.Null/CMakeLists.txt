# Foundation::Shell.Null - `foundation.shell.null` (headless IShell).

add_library(Shell.Null STATIC)
add_library(Foundation::Shell.Null ALIAS Shell.Null)

target_link_libraries(Shell.Null PUBLIC Foundation::Shell Foundation::Policy)

target_sources(Shell.Null
    PUBLIC
        FILE_SET shell_null_modules TYPE CXX_MODULES FILES
            NullShell.cppm)

