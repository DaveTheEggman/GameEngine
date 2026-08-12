# Particle authoring + cooked runtime resource

> Status: CURRENT
> Verified: 2026-08-12 @ 9812521b
> Track: [[particles-plan]]

The two tiers that turn the shipped CPU particle runtime (`particles.md`) into an artist-authorable,
cooked-at-build asset: the cooked runtime resource and the editor asset + builder. Implemented; the
bespoke authoring PAGE and LUT curve baking are the deferred pieces.

## Cooked runtime resource (`foundation.particles.resource`)

`ParticleEffectResource` - the cooked runtime input - with its factory + resource-manager registration,
content-DB GUID keying, and a bidirectional serializer. `ParticleEffectComponent` carries an opaque ref
resolved through the standard `resource::Ref` + scene resolve pass, and reloads live: ref resolution +
hot-reload `Proxy`s are done (not deferred), so an edited + recooked effect swaps behind the component.
The runtime path is `Data/Output/ParticleFX/*.rasset` -> load -> render.

## Editor asset + builder (`particles.pipeline`)

`ParticleEffectAsset` (source) + a builder (`Status Build(const editor::Asset&,
editor::AssetBuildContext&)`) that cooks the source effect to `ParticleEffectResource`, registered with
the pipeline registration composition root. The `ParticleFX` sample exercises the full authored ->
cooked -> loaded -> rendered path.

## Deferred

- **The bespoke ParticleEffectPage** (a tree + reflection-driven inspector + curve/gradient controls +
  live preview) - deferred; effects are edited through the generic reflected inspector until it lands.
- **LUT curve baking.** Over-lifetime behaviors evaluate their curves directly today; baking curves to
  LUTs would mean changing those shipped behaviors, so it is deferred until a measured need (it is
  independent of authoring).
