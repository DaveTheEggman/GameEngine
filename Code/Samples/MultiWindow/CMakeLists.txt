# MultiWindow — runtime-host smoke test: two OS windows, each cleared a different
# color through the shared GraphicsDevice. Needs the Vulkan GPU factory + desktop.

add_executable(MultiWindow main.cpp)

target_link_libraries(MultiWindow PRIVATE
    Draconic::Core
    Draconic::RHI
    Draconic::Runtime
    Draconic::RuntimeClient
    Draconic::RuntimePlatform
    Draconic::RuntimePlatformDesktop
    Draconic::RuntimeGraphics
    Draconic::RuntimeGraphicsGpu
    Draconic::Policy)
draconic_copy_sdl3_dll(MultiWindow)
