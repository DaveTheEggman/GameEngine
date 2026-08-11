# Foundation::Script.Wren - the Wren VM backend for Foundation::Script.
#
# The C++-module backend implementing IScriptManager / IScriptContext over the vendored Wren VM
# (ThirdParty::Wren - fetched + built in ThirdParty/CMakeLists.txt, gated on OPTION_ENABLE_WREN,
# alongside the by-copy luau/angelscript vendors).

add_library(Script.Wren STATIC)
add_library(Foundation::Script.Wren ALIAS Script.Wren)

# ThirdParty::Wren is PRIVATE: consumers see only the foundation.script.wren module (the factory),
# never the Wren C API. (CMake still links it into final exes.) Foundation::Script.Facades PUBLIC:
# WrenScript.cppm imports it for BehaviorFacadeNames() (the neutral facade-name list its
# behavior-module prelude is built from).
target_link_libraries(Script.Wren PUBLIC Foundation::Script Foundation::Script.Facades Foundation::Policy PRIVATE ThirdParty::Wren)

target_sources(Script.Wren
    PUBLIC
        FILE_SET wren_modules TYPE CXX_MODULES FILES
            WrenScript.cppm)
