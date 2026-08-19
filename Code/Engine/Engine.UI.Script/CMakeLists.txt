# Engine::UI.Script - `engine.ui.script`: the UI script surface (reflected typed view handles + the `ui`
# screen-tier facade over the gamekit ScreenStack). Out-of-tree facade module (the facade-pattern rule)
# consuming CORE foundation.ui + foundation.ui.gamekit; joins RegisterAllScriptFacades. The app installs
# its per-context service (screen root + stack + document instantiator); this library holds no live
# subsystem state, so it stays headless-linkable.
add_library(Engine.UI.Script STATIC)
add_library(Engine::UI.Script ALIAS Engine.UI.Script)

target_link_libraries(Engine.UI.Script
    PUBLIC Foundation::Core Foundation::UI Foundation::UI.Gamekit
           Foundation::Script Foundation::Script.Facades Foundation::Policy)

target_sources(Engine.UI.Script
    PRIVATE
        UiScriptImpl.cpp   # handle + facade bodies + REFLECT registrations
    PUBLIC
        FILE_SET engine_uiscript_modules TYPE CXX_MODULES FILES
            UiScriptTypes.cppm
            UiScriptFacade.cppm
            UiScriptModule.cppm)
