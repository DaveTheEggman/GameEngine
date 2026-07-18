# Draconic::PhysicsSubsystem - `draconic.physics.subsystem`: scene integration
# (components + per-scene world + sync + interpolation + debug draw).
add_library(DraconicPhysicsSubsystem STATIC)
add_library(Draconic::PhysicsSubsystem ALIAS DraconicPhysicsSubsystem)

target_link_libraries(DraconicPhysicsSubsystem
    PUBLIC Draconic::Core Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::Physics Draconic::PhysicsResource Draconic::Resource Draconic::Script
           Draconic::RenderSubsystem Draconic::Policy)

target_sources(DraconicPhysicsSubsystem
    PRIVATE
        SubsystemImpl.cpp   # implementation unit: render-frame drive (render dep stays out of the interface)
    PUBLIC
        FILE_SET physics_subsystem_modules TYPE CXX_MODULES FILES
            Subsystem.cppm
            Components.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
