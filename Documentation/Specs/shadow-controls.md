# Shadow controls - sane biases, and the controls to tune them in the editor

> STATUS: PROPOSED 2026-10-03 (user: "write it up"). Shared with Sedulous, which has the same
> hard-coded values; both trees build it, after Sedulous's sync of 2026-10-03 is done
> (`~/Dev/Beef/SedulousEngine/Documentation/RaptorSync.md`), so no code changes under it.

## The problem

PaperKid's house walls show a blotchy, diagonal mottling: shadow acne. Found while tuning its
look (2026-10-03): it is there with ambient occlusion off and gone with the sun's shadows off,
on walls the sun meets at a shallow angle.

- The sun's shadow biases are constants, not settings: `MeshRendererImpl.cpp:1005`
  (`shadowNormalBias = 0.02f`, `shadowDepthBias = 0.0009f`). Sedulous has the same values
  (`MeshRenderer.bf:1345`) and its terrain renderer its own copy (`TerrainRenderer.bf:30`).
- The normal offset is scaled by the cascade's world texel size and `1 - NdotL`
  (`forward.ps.hlsl:81`), so 0.02 pushes a surface out by 2% of a shadow texel: effectively no
  offset, which is what lets a grazing wall shadow itself. Normal offsets are usually about one
  to two texels.
- A light offers only `castsShadows` and `shadowUpdate`; nothing a scene author can tune. The
  only scene-side workaround is turning the sun, which moves the acne rather than removing it.

## Proposal

### 1. Defaults that work untouched

- The normal offset in shadow texels, default about **1.0** (measured, not assumed: see Tests),
  the depth bias re-checked against it. A scene that never touches the settings loses the acne.
- One source for the values: the terrain renderer reads the same defaults (or the light's
  values), never a copy of its own.

### 2. Shadow controls on the light

On the light component, shown only while `castsShadows` is on (`visibleWhen`, as the camera's
perspective and orthographic fields are):

- `shadowDepthBias`: the depth bias, as today's constant, default the re-checked value.
- `shadowNormalBias`: the normal offset in shadow texels, default 1.0.
- `shadowStrength` (0 to 1): how dark the light's shadows get, 1 = full. An art-direction
  control, and a way to soften a scene without changing its light.

Each light's values reach the shadow sampling it owns: the sun's cascades through the view
constants that carry today's constants; a local light's atlas entry through its per-light data
(`depthBias` is already there, `forward.ps.hlsl:139`). The light's data version bumps; older
scenes read the defaults.

Unity, Unreal and Godot all put these three on the light.

### 3. Later, if tuning shows the need: per-scene cascade settings

Shadow distance, cascade split and resolution as a scene settings block. Not part of this unless
tuning the biases shows a scene needs them.

## Tests

- The default normal offset removes the acne on a grazing wall: a render test of a wall at a
  shallow angle to a directional light, sampling its lit face for false shadow, failing with
  today's values and passing with the new defaults.
- A light's bias and strength reach the sampling: a light with `shadowStrength` 0 casts no
  darkening; 0.5 darkens half as much as 1.
- Serialization: the new fields round-trip; a scene at the previous light data version reads
  the defaults.
- The inspector shows the three only while the light casts shadows (the reflection attributes).
- PaperKid's look, before and after: the house walls' mottling gone at the defaults (measured
  the way its tuning measures, `Tools/look.py`, plus a crop of the near wall).

## Not in scope

Contact-hardening or ray-traced shadows; shadow filtering changes; the cascade settings unless
step 3 is wanted.
