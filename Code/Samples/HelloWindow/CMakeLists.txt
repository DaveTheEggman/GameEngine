# HelloWindow — minimal windowed Raptor app: opens an SDL3 window, clears it each
# frame through a GraphicsDevice, runs until closed. Needs the Vulkan GPU factory
# (so the window actually presents) in addition to the desktop platform.

add_executable(HelloWindow main.cpp)

target_link_libraries(HelloWindow PRIVATE
    Raptor::Core
    Raptor::Runtime
    Raptor::RuntimeClient
    Raptor::RuntimePlatformDesktop
    Raptor::RuntimeGraphics
    Raptor::RuntimeGraphicsGpu
    Raptor::Policy)
raptor_copy_sdl3_dll(HelloWindow)
