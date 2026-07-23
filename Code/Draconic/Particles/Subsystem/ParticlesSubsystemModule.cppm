// draconic.particles.subsystem - scene/render integration for the particle system: the ECS
// component + manager (which ticks the CPU sim and provides billboard render-data), the dedicated
// ParticleRenderer, and the Context-level ParticleSubsystem that wires them into RenderSubsystem via
// its generic register-renderer / register-provider seam. Depends on render.subsystem + scene +
// draconic.particles; draconic.render stays ignorant of particles. See docs/design/particles.md.

export module draconic.particles.subsystem;

export import :renderdata; // ParticleBillboardInstance + ParticleBillboardRenderData
export import :particle_shaders; // ParticleVS()/ParticlePS()/TrailVS()/TrailPS() HLSL source (pass-internal)
export import :renderer;   // ParticleRenderer (dedicated billboard Renderer)
export import :components; // ParticleEffectComponent + manager (sim tick + IRenderDataProvider)
export import :subsystem;  // ParticleSubsystem (injects manager, registers renderer + provider)
