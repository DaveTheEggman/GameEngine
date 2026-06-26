# Raptor::RuntimeClient — `raptor.runtime.client` (the Application base class).
# Loop-agnostic: the platform layer drives Start/Tick/Stop.

add_library(RaptorRuntimeClient STATIC)
add_library(Raptor::RuntimeClient ALIAS RaptorRuntimeClient)

target_link_libraries(RaptorRuntimeClient
    PUBLIC Raptor::Runtime Raptor::RuntimePlatform Raptor::RuntimeGraphics Raptor::Policy)

target_sources(RaptorRuntimeClient
    PUBLIC
        FILE_SET runtime_client_modules TYPE CXX_MODULES FILES
            Application.cppm
            ApplicationHost.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
