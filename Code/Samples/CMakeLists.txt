# Raptor sample applications. Built when their required backends are available.

if(TARGET Raptor::RuntimePlatformDesktop)
    add_subdirectory(HelloWindow)
else()
    message(STATUS "Samples: desktop platform unavailable (SDL3 not found) — skipping HelloWindow")
endif()
