# MultiWindow - runtime-host smoke test: two OS windows, each cleared a different
# color through the shared GraphicsDevice. Needs the Vulkan GPU factory + desktop.

add_executable(MultiWindow main.cpp)

target_link_libraries(MultiWindow PRIVATE
    Foundation::Core
    Foundation::RHI
    Foundation::Runtime
    Foundation::Runtime.Client
    Foundation::Shell
    Foundation::Shell.Desktop Foundation::Runtime.Desktop
    Foundation::Graphics
    Foundation::Graphics.Gpu
    Foundation::Policy)
draconic_copy_sdl3_dll(MultiWindow)
