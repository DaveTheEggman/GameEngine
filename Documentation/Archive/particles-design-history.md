# Particles - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/particles.md (+ particles-authoring.md)
> Track: [[particles-plan]]

NON-AUTHORITATIVE. The 8-engine survey + recommended plan behind the particle system. Present-tense
truth is the two particles Systems docs; the full original doc is in git at the P0 commit 3b92560d.
Kept for the "why".

## The survey

Synthesized from ezEngine, SedulousEngine, FlaxEngine, LumixEngine, ogre-next (ParticleFX / FX2),
Godot 4, PlayCanvas, and Babylon.js. Grounded in the shipped substrate: the sprite/billboard renderer
([[sprites-and-dynamic-categories]]) + the instanced-mesh primitive ([[instanced-mesh]]).

## The recommended plan

- **GPU-compute-first, rendered through the instanced-mesh path.** Both CPU and GPU sims draw through
  an instancing path we already have - cleaner than a bespoke particle renderer.
- **Port Sedulous's CPU model first** (its SoA `ParticleStreamId` streams + module stacks are the
  port-of-least-resistance skeleton on Draconic's exact idioms), then fill in the GPU-compute simulator
  Sedulous only stubbed.
- **The CPU/GPU seam** is Sedulous's: `SimulationMode{CPU,GPU,Auto}` + per-behavior
  `BehaviorSupport{CPUOnly,GPUOnly,Both}`. Sedulous shipped the seam but its `GPUSimulator` falls back to
  CPU; the plan implements the GPU path (Godot/Flax design: a per-effect compute sim writing the
  `InstanceData` buffer + an indirect draw keyed on the GPU particle counter).

## How it landed

The CPU simulator + all modules + render integration + the cooked-resource/authoring tiers shipped. The
GPU-compute simulator remains the one deferred piece (the seam - mode resolution + support flags -
exists; the compute simulator does not). The bespoke authoring page + LUT curve baking are also
deferred.
