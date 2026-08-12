# Particles / VFX

> Status: CURRENT
> Verified: 2026-08-12 @ 9812521b
> Track: [[particles-plan]]

A data-driven particle system whose render path is the shipped instanced-mesh / sprite path. The CPU
simulator + all behavior modules + render integration shipped; the GPU-compute simulator is a wired seam
whose implementation is deferred. Authoring + the cooked runtime resource: `particles-authoring.md`.

## Modules

- **`foundation.particles`** (`Code/Foundation/Particles`) - the runtime: `ParticleEffect` (the sim
  driver + mode resolution), `ParticleModules` (`ParticleEmitter` + init/update behavior modules),
  `ParticleStreams` (SoA particle data, `ParticleStreamId`), `ParticleTypes`.
- **`foundation.particles.resource`** - the cooked `ParticleEffectResource` (see authoring doc).
- **`particles.pipeline`** - `ParticleEffectAsset` + builder.
- **`engine.particles`** - `ParticleEffectComponent` + `ParticleSystem` (the scene subsystem).

## Runtime (shipped)

An effect is a set of emitters, each a stack of behavior modules over an SoA particle stream (spawn,
lifetime, over-lifetime color/size/velocity, forces, ...). The CPU simulator runs the modules per frame
and feeds the render path. Rendering goes through the shipped sprite/billboard + instanced-mesh path
(one instanced draw per effect batch - the same `InstanceData` machinery as static/skinned instancing),
so particles cost the renderer nothing new. Effects reference their cooked resource via a hot-reload
`Proxy`.

## The CPU/GPU seam

Sedulous shipped the seam but stubbed the GPU simulator; the seam is ported: `SimulationMode` (CPU / GPU
/ Auto) with per-module `BehaviorSupport` (CPUOnly / GPUOnly / Both), and `ResolveSimulationMode`
picking a mode (Auto -> GPU when all modules support it and `maxParticles > 1024`, else CPU). The CPU
path is fully implemented behind this seam.

## Deferred

- **The GPU-compute simulator.** Only the seam exists (mode resolution + per-module support flags);
  there is no compute-shader simulator (no particle `numthreads` shaders, no `GPUSimulator` dispatch).
  The plan (Godot/Flax-style): a per-effect compute sim writing the existing `InstanceData` buffer +
  an indirect draw keyed on the GPU particle counter. CPU is fine for typical emitters; GPU is the
  ceiling-breaker.

---

The 8-engine architecture survey (ezEngine / Sedulous / Flax / Lumix / ogre-next / Godot 4 / PlayCanvas /
Babylon.js) and the recommended-plan writeup are in `Documentation/Archive/particles-design-history.md`.
