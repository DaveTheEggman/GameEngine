# GameUiSandbox - the game-UI-kit widgets (MenuList / Bar / Ticker / Toast / ButtonPrompt) on screen,
# pushed onto the UISubsystem's screen-tier ScreenStack from a DefaultApplication.
add_executable(GameUiSandbox main.cpp)

target_link_libraries(GameUiSandbox PRIVATE
    Foundation::Core
    Foundation::Runtime Foundation::Runtime.Client
    Foundation::Shell.Desktop Foundation::Runtime.Desktop
    Foundation::Graphics Foundation::Graphics.Gpu
    Engine::DefaultApp
    Foundation::Scene Engine::Scene Engine::Render
    Engine::UI Foundation::UI Foundation::UI.Gamekit
    Foundation::Input
    Foundation::Policy)
util_copy_sdl3_dll(GameUiSandbox)

target_compile_definitions(GameUiSandbox PRIVATE
    BUILTIN_GAMEUI_FONT="${CMAKE_SOURCE_DIR}/Data/Assets/fonts/roboto/Roboto-Regular.ttf")
