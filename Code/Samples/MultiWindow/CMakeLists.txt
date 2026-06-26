# MultiWindow — runtime-host smoke test: two OS windows, each cleared a different
# color through the shared GraphicsDevice. Needs the Vulkan GPU factory + desktop.

add_executable(MultiWindow main.cpp)

target_link_libraries(MultiWindow PRIVATE
    Raptor::Core
    Raptor::RHI
    Raptor::Runtime
    Raptor::RuntimeClient
    Raptor::RuntimePlatform
    Raptor::RuntimePlatformDesktop
    Raptor::RuntimeGraphics
    Raptor::RuntimeGraphicsGpu
    Raptor::Policy)
