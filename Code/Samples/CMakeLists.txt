# Raptor sample applications. Built when their required backends are available.

# HelloWindow and MultiWindow both present through a GraphicsDevice, so both need
# the desktop platform + the Vulkan GPU factory.
if(TARGET Raptor::RuntimePlatformDesktop AND TARGET Raptor::RuntimeGraphicsGpu)
    add_subdirectory(HelloWindow)
    add_subdirectory(MultiWindow)
else()
    message(STATUS "Samples: skipping HelloWindow/MultiWindow (need desktop platform + Vulkan GPU factory)")
endif()

# RHI sample framework + RHI samples (need Vulkan + Shaders too).
add_subdirectory(Framework)
if(TARGET Raptor::SamplesFramework)
    add_subdirectory(RHI)
    add_subdirectory(VG)  # VG sandbox — end-to-end VG stack demo
endif()
