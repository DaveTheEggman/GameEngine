# Draconic::ParticlesSubsystem - `draconic.particles.subsystem`: scene/render integration for the
# particle system (ECS component + manager + dedicated ParticleRenderer + ParticleSubsystem). Links
# render.subsystem (the register-renderer/register-provider seam), scene, and the CPU particle core.
add_library(DraconicParticlesSubsystem STATIC)
add_library(Draconic::ParticlesSubsystem ALIAS DraconicParticlesSubsystem)

target_link_libraries(DraconicParticlesSubsystem
    PUBLIC Draconic::Core Draconic::RHI Draconic::Shaders Draconic::ShaderSystem
           Draconic::Runtime Draconic::Scene Draconic::SceneSubsystem
           Draconic::Render Draconic::RenderSubsystem Draconic::Particles
           Draconic::Geometry Draconic::Materials Draconic::Policy)

target_sources(DraconicParticlesSubsystem
    PUBLIC
        FILE_SET particles_subsystem_modules TYPE CXX_MODULES FILES
            RenderData.cppm
            ParticleRenderer.cppm
            Components.cppm
            Subsystem.cppm
            ParticlesSubsystemModule.cppm)
