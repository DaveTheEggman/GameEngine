# Foundation::Script.AngelScript - the AngelScript backend for Foundation::Script.
#
# Builds on the vendored ThirdParty/angelscript (ThirdParty::AngelScript). The SDK
# is PRIVATE: consumers see only the draconic.script.angelscript module (the
# factory + registry registration), never the AngelScript C++ API.

add_library(Script.AngelScript STATIC)
add_library(Foundation::Script.AngelScript ALIAS Script.AngelScript)

target_link_libraries(Script.AngelScript
    PUBLIC Foundation::Script Foundation::Policy
    PRIVATE ThirdParty::AngelScript)

target_sources(Script.AngelScript
    PRIVATE
        AngelScriptScriptImpl.cpp   # module IMPLEMENTATION unit: all SDK contact
    PUBLIC
        FILE_SET angelscript_modules TYPE CXX_MODULES FILES
            AngelScriptScript.cppm)

