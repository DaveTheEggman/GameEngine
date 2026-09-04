# Foundation::UI.Gamekit - `foundation.ui.gamekit`: game-UI conveniences layered on foundation.ui,
# sibling of foundation.ui.toolkit. Provides a UIScreen (a page/screen view with 3 input modes +
# declared transition + default focus) and a ScreenStack (push/pop/replace over a RootView, with focus
# save/restore, modal input shielding, and built-in tween transitions on the CORE Animation layer).
# Pure view-layer: no scene/render/script deps. Consumers write `import foundation.ui.gamekit;`.
util_add_engine_library(UI.Gamekit ALIAS Foundation::UI.Gamekit)
target_link_libraries(UI.Gamekit
    # Foundation::Input is the one non-view dep: ButtonPrompt renders an action's binding via
    # foundation.input's DescribeBinding (the shared :binding_names helpers).
    PUBLIC Foundation::Core Foundation::UI Foundation::VG Foundation::Fonts Foundation::Input Foundation::Policy)

target_sources(UI.Gamekit
    PRIVATE
        GamekitImpl.cpp   # ScreenStack push/pop/transition drive + `<screen>` markup registration
        WidgetsImpl.cpp   # game-UI widgets (MenuList, ...): RTTI + logic
    PUBLIC
        FILE_SET uigamekit_modules TYPE CXX_MODULES FILES
            UIScreen.cppm
            ScreenStack.cppm
            MenuList.cppm
            Bar.cppm
            Ticker.cppm
            Toast.cppm
            ButtonPrompt.cppm
            GamekitModule.cppm)
