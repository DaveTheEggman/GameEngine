# Shadow controls - the controls to tune a light's shadow, and what the mottling was

## The problem

PaperKid's house walls show a blotchy, chevron mottling under the roof. Found while tuning its
look (2026-10-03): it is there with ambient occlusion off and gone with the sun's shadows off,
on the walls the sun meets at a shallow angle (N.L ~ 0.17).

- A light offers only `castsShadows` and `shadowUpdate`; nothing a scene author can tune. The sun's
  biases are constants (`MeshRendererImpl.cpp`, `TerrainRenderer.cppm`; Sedulous's
  `MeshRenderer.bf:1345` and `TerrainRenderer.bf:30` the same).
- First read as shadow acne from the near-zero normal offset (0.02 texels). Measured, it is not:
  a real-device probe of PaperKid's wall, sun and camera (Render.Backend.Tests,
  ShadowControlProbeTests) reads clean across a sweep of offsets from 0.02 to 2 texels; the
  casters' slope-scaled hardware bias carries acne. Raising the offset to 1 texel leaves the
  mottling in the game unchanged.
- What it is: the roof overhang's shadow, under-resolved. The cascades fit the camera's far plane
  (PaperKid's 400 m, clamped to the 300 m shadow reach), so a near cascade's texel is large, and
  the grazing sun stretches it about six times across the wall: the shadow edge breaks into
  chevrons. With the camera's far plane at 60 m the same frame shows the roof's shadow as a clean
  band (2026-10-03, look.py crops).

## Built (shadow-controls branch)

### 1. Shadow controls on the light

On the light component (data version 1), shown only while `castsShadows`:

- `shadowStrength` (0 to 1): how dark the light's shadow gets, 1 = full. The forward and terrain
  shaders lerp the shadow toward lit by it.
- `shadowNormalBias`: the normal offset in shadow texels, default 0.02 (the old constant, kept: see
  above). Local lights get it too, as world units per unit of distance from the light (their tile's
  texel size), so it means the same thing for every light.
- `shadowDepthBiasScale`: the depth-compare bias as a scale of the light type's default (1 =
  default). A scale, not a value, because the sun's and a local light's depths are in different
  spaces (0.0009 and 0.0015 NDC); Unreal's Shadow Bias is relative the same way.

One source for the defaults (`render::ShadowBiasDefaults`); the terrain renderer reads the light's
values from the cascades instead of its own copy. The stored scenes with a light (PaperKid's six,
Sky Hopper's three) are re-stamped at version 1 with the defaults.

### 2. Next: per-scene cascade settings (what fixes the mottling)

Tuning shows the need the spec anticipated: shadow distance (how far the cascades reach,
independent of the camera's far plane), the cascade split, and resolution, as a scene settings
block. PaperKid wants a short reach (its streets are tens of metres); Sky Hopper a longer one.

## Tests

- Real device: a wall the sun grazes reads clean across the offset sweep and at the default; a
  cube's shadow darkens fully at strength 1, half as much at 0.5, and the open plane is untouched.
- Extraction hands each control to the shadow it casts (the sun's cascades, a local light's atlas
  entry with its normal offset per distance, the per-light strength).
- The controls round-trip with the scene at version 1; the inspector shows them only while the
  light casts shadows.

## Not in scope

Contact-hardening or ray-traced shadows; shadow filtering changes.
