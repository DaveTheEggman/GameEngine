# Raptor sample applications. Built when their required backends are available.

# Helper: copy SDL3.dll next to sample executables on Windows.
function(raptor_copy_sdl3_dll target)
    if(RAPTOR_SDL3_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RAPTOR_SDL3_DLL}" "$<TARGET_FILE_DIR:${target}>")
    endif()
endfunction()

# HelloWindow and MultiWindow both present through a GraphicsDevice, so both need
# the desktop platform + the Vulkan GPU factory.
if(TARGET Raptor::RuntimePlatformDesktop AND TARGET Raptor::RuntimeGraphicsGpu)
    add_subdirectory(HelloWindow)
    add_subdirectory(MultiWindow)
    add_subdirectory(Sandbox)        # the dev harness we grow the engine in
else()
    message(STATUS "Samples: skipping HelloWindow/MultiWindow (need desktop platform + Vulkan GPU factory)")
endif()

# RHI sample framework + RHI samples (need Vulkan + Shaders too).
add_subdirectory(Framework)
if(TARGET Raptor::SamplesFramework)
    add_subdirectory(RHI)
    add_subdirectory(VG)  # VG sandbox — end-to-end VG stack demo
endif()
