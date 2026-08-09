# Foundation::Script.Luau - the Luau VM backend for Foundation::Script.
#
# Builds the C++-module backend over the vendored Luau interpreter + compiler
# (ThirdParty::Luau - full-tree vendor, interpreter-only targets, LUA_USE_LONGJMP=1
# for the -fno-exceptions engine). Consumers see only the foundation.script.luau
# module (the factory), never the Luau C API (the lib link is PRIVATE).

add_library(Script.Luau STATIC)
add_library(Foundation::Script.Luau ALIAS Script.Luau)

target_link_libraries(Script.Luau
    PUBLIC Foundation::Script Foundation::Policy
    PRIVATE ThirdParty::Luau)

target_sources(Script.Luau
    PUBLIC
        FILE_SET luau_modules TYPE CXX_MODULES FILES
            LuauScript.cppm)
