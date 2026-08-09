# Engine::Integration - `engine.integration`: cross-subsystem composition
# helpers owned by the app. Depends on BOTH sides of each bridge so neither subsystem gains a
# dependency on the other. First tenant: the physics-contact -> script bridge.
add_library(Engine.Integration STATIC)
add_library(Engine::Integration ALIAS Engine.Integration)

target_link_libraries(Engine.Integration
    PUBLIC Foundation::Core Foundation::Scene Foundation::Physics
           Engine::Physics Engine::Script Foundation::Policy)

target_sources(Engine.Integration
    PRIVATE
        ScriptPhysicsContactBridge.cpp   # implementation unit: the virtual + subsystem calls
    PUBLIC
        FILE_SET integration_modules TYPE CXX_MODULES FILES
            ScriptPhysicsContactBridge.cppm)
