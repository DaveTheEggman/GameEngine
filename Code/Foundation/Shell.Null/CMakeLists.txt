# Foundation::Shell.Null - `foundation.shell.null` (headless IShell).

util_add_engine_library(Shell.Null ALIAS Foundation::Shell.Null)
target_link_libraries(Shell.Null PUBLIC Foundation::Shell Foundation::Policy)

target_sources(Shell.Null
    PUBLIC
        FILE_SET shell_null_modules TYPE CXX_MODULES FILES
            NullShell.cppm)

