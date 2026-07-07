# Draconic sample applications. Built when their required backends are available.

# Helper: copy SDL3.dll next to sample executables on Windows.
function(draconic_copy_sdl3_dll target)
    if(DRACONIC_SDL3_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DRACONIC_SDL3_DLL}" "$<TARGET_FILE_DIR:${target}>")
    endif()
endfunction()

# HelloWindow and MultiWindow both present through a GraphicsDevice, so both need
# the desktop shell + the Vulkan GPU factory.
if(TARGET Draconic::ShellDesktop AND TARGET Draconic::RuntimeDesktop AND TARGET Draconic::GraphicsGpu)
    add_subdirectory(HelloWindow)
    add_subdirectory(MultiWindow)
    add_subdirectory(Sandbox)            # the dev harness we grow the engine in
    add_subdirectory(RenderStressTest)   # worst-case benchmark to keep perf honest
    add_subdirectory(AnimStressTest)     # animation/skinning benchmark (Sandbox copy, stripped toward a grid)
    add_subdirectory(AnimatedCrowd)     # skinned-crowd instancing (InstancedMeshComponent + InstancedSkinning)
else()
    message(STATUS "Samples: skipping HelloWindow/MultiWindow (need desktop shell + Vulkan GPU factory)")
endif()

# RHI sample framework + RHI samples (need Vulkan + Shaders too).
add_subdirectory(Framework)
if(TARGET Draconic::SamplesFramework)
    add_subdirectory(RHI)
    add_subdirectory(VG)  # VG sandbox - end-to-end VG stack demo
endif()
