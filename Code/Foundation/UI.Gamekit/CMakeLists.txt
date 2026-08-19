# Foundation::UI.Gamekit - `foundation.ui.gamekit`: game-UI conveniences layered on foundation.ui,
# sibling of foundation.ui.toolkit. Provides a UIScreen (a page/screen view with 3 input modes +
# declared transition + default focus) and a ScreenStack (push/pop/replace over a RootView, with focus
# save/restore, modal input shielding, and built-in tween transitions on the CORE Animation layer).
# Pure view-layer: no scene/render/script deps. Consumers write `import foundation.ui.gamekit;`.
add_library(UI.Gamekit STATIC)
add_library(Foundation::UI.Gamekit ALIAS UI.Gamekit)

target_link_libraries(UI.Gamekit
    PUBLIC Foundation::Core Foundation::UI Foundation::Policy)

target_sources(UI.Gamekit
    PRIVATE
        GamekitImpl.cpp   # ScreenStack push/pop/transition drive + `<screen>` markup registration
    PUBLIC
        FILE_SET uigamekit_modules TYPE CXX_MODULES FILES
            UIScreen.cppm
            ScreenStack.cppm
            GamekitModule.cppm)
