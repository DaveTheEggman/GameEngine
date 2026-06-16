# HelloWindow — minimal windowed Raptor app (opens an SDL3 window, runs until closed).

add_executable(HelloWindow main.cpp)

target_link_libraries(HelloWindow PRIVATE
    Raptor::Core
    Raptor::Runtime
    Raptor::RuntimeClient
    Raptor::RuntimePlatformDesktop
    Raptor::Policy)
