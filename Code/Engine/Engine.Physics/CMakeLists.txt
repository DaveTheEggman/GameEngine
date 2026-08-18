# Engine::Physics - `engine.physics`: scene integration
# (components + per-scene world + sync + interpolation + debug draw).
add_library(Engine.Physics STATIC)
add_library(Engine::Physics ALIAS Engine.Physics)

target_link_libraries(Engine.Physics
    PUBLIC Foundation::Core Foundation::Runtime Foundation::Scene Engine::Scene
           Foundation::Physics Foundation::Physics.Resource Foundation::Resource Foundation::Script
           Foundation::Script.Facades Engine::Render Foundation::Profiler Foundation::Policy)

target_sources(Engine.Physics
    PRIVATE
        PhysicsSubsystemImpl.cpp   # implementation unit: render-frame drive (render dep stays out of the interface)
    PUBLIC
        FILE_SET physics_subsystem_modules TYPE CXX_MODULES FILES
            PhysicsSubsystem.cppm
            PhysicsComponents.cppm)

