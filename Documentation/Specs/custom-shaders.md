# Custom shaders - a game's own surfaces, written in the editor

> Status: APPROVED 2026-10-12 (the decisions below). Phase 0 built. Spiked:
> `Code/Samples/CustomShader` (2026-10-12) proves the low-level kind at the engine level. Two
> kinds are supported (user, 2026-10-12): low-level shaders and surface shaders.
> Track: 0.1 backlog, "Custom shaders through the editor, before the 0.1 release".
> Mirror: Sedulous ports each phase after it lands.
> Read CONVENTIONS.md first.

## Where things stand

**At the engine level, low-level custom shaders work as designed** (747cba0f, "material-driven
set-2 layout + PSO": "a custom shader with a different property set gets its own set-2 layout +
PSO with zero renderer changes (the foundation for custom shaders like toon)"). The
`CustomShader` sample proves it, ported from legacy Sedulous's `RenderMaterialsCustom` (BeefGFX):
a toon shader registered by name (`ShaderSystem::RegisterSource`), a material built in code whose
properties, in order, lay out the shader's set-2 block, and nothing in the renderer told about
it. It draws distinct and instanced meshes, a masked material, the skinned Fox, receives the
sun's cascades and a point light's cube shadow, and writes the motion vectors TAA reprojects
with. The contract a low-level shader keeps: the mesh vertex in, `forward_vs.hlsli`'s VSOutput
(or its own equivalent), sets 0, 1 and 3 declared as `forward.ps.hlsl` declares them, its own set
2, the SKINNED / INSTANCED / WIND and ALPHA_TEST / GBUFFER / DITHER variants, and under GBUFFER
the five targets. It forks the whole lighting; that is the point of this kind.

What does not exist is everything between that and a user of the editor or a shipped game:

- **Authoring.** A `ShaderAsset` (`Pipeline/Shaders.Pipeline/ShaderAsset.cppm`) is a name plus
  two HLSL files, vertex and fragment, each with a `main`: the low-level kind's shape. Nothing
  creates one (no Create entry; a `.hlsl` file imports as nothing), it opens in the generic form
  page, and a material's parameters can only be declared in code (`MaterialBuilder`).
- **The cook copies text.** `ShaderAssetBuilder` reads both files into a `ShaderSource` and never
  compiles them, so broken HLSL cooks "successfully". Included files are not tracked.
- **Errors surface at draw time.** The first draw compiles the variant through DXC; a failure is
  `Shader variant compile failed: ...` in the log, every frame (not cached), and the old modules
  and pipeline are already gone, so the mesh vanishes. The hot-reload doc's "keep the old on
  failure" is not what the code does.
- **Exported games cannot run one.** A dist ships no DXC; a registered source logs "cannot be
  compiled in compiler-free pack mode" and draws nothing. Only the engine corpus (`Data/Shaders`)
  is precompiled, to SPIR-V, DXIL and WGSL (DXC, then naga and tint for the web).
- **Depth, shadow and pick passes use built-in shaders.** A custom vertex shader's displacement
  never reaches them, and masked shadows assume `AlbedoMap` at t0.
- **The material page** has a read-only Shader row, and its preview ignores `shaderId`.
- **No include asset exists** (the backlog says there is one). Includes resolve against
  `Data/Shaders` in dev builds only (the toon shader includes `forward_vs.hlsli` that way).
- The engine's own depth and shadow pipelines draw with Vulkan validation warnings ("Vertex
  attribute at location 1..4 not consumed by vertex shader"), in every sample with shadows: a
  position-only shader on the full mesh layout. Harmless, but noise to fix on the way.

## Two kinds, for two purposes

**Low-level shaders** (what the asset holds today, and what the toon sample is). Full control: a
whole shading model of one's own (toon, a stylised water, a hologram), at the price of keeping the
frame's contract above, and following it when the renderer changes. For engine-literate users
and effects the surface model cannot express.

**Surface shaders.** The user writes what makes the surface theirs, and the engine writes the
rest:

```hlsl
// A lava surface: scrolling, glowing cracks.
properties
{
    Color   CrackColor = (1.0, 0.35, 0.05, 1.0);
    float   Speed      = 0.2   [0, 2];
    float   Glow       = 4.0   [0, 20];
    Texture Cracks;
}

void Surface(SurfaceInput input, inout SurfaceOutput surface)
{
    float2 uv = input.uv + float2(0.0, Time() * Speed);
    float crack = Cracks.Sample(MainSampler, uv).r;
    surface.albedo = lerp(float3(0.08, 0.05, 0.04), CrackColor.rgb, crack);
    surface.roughness = 0.9;
    surface.emissive = CrackColor.rgb * crack * Glow;
}

// Optional: move the vertex (world space), e.g. waves.
// void Vertex(inout VertexInput input) { ... }
```

The engine wraps it in its own templates: the forward vertex shader (skinning, instancing, wind),
the lighting and IBL, the five targets and velocity, the depth and shadow passes. `SurfaceOutput`
carries albedo, alpha, normal (tangent space), metallic, roughness, occlusion and emissive; a
`lit` or `unlit` model is declared at the top. What the user gets for free: every light, shadow,
probe, post-processing pass and TAA works, skinned and instanced meshes work, and a renderer change
updates every user shader with it. This is what Unity's surface shaders and Godot's shader
language give, and it is exactly what a node graph would generate later (tier 2), so the two
tiers meet instead of competing.

Both are shader assets, and a material picks either the same way. They share everything below
the authoring: compiled at cook time, shipped as bytecode, the editor's shader page, the material
page's picker and rows. A surface shader is a low-level shader the engine writes for you.

### Properties: declared in the shader

The `properties` block is the one place a parameter is declared, in either kind. In a surface shader
the engine generates from it the material cbuffer (16-byte packed), the texture and sampler
declarations at the set-2 slots `MaterialSystem` assigns, and the material's property table: names,
types, defaults, ranges for the editor's sliders. No reflection of compiled shaders is needed, and
the layout cannot drift from the code because both come from one declaration. Types: `float`,
`float2..4`, `int`, `Color` (sRGB, linear in the shader), `ColorHdr` (with an intensity, as the PBR
emissive is), `Texture` (2D), `TextureCube`. A low-level shader carries the same block (it replaces
the `MaterialBuilder` calls the toon sample makes in code) and declares its own set-2 block to
match; the cook checks the two agree. A material on either shows exactly these rows; switching a
material to another shader keeps the values whose names and types match.

## How it compiles, ships and reloads

- **Compiled at cook time.** The shader asset's builder assembles template plus surface, compiles
  every variant the templates use (vertex: SKINNED, INSTANCED, WIND; pixel: ALPHA_TEST, GBUFFER,
  DITHER; the depth/shadow pair) for the running editor's backend, and fails the cook on an error.
  Errors are mapped back to the user's file and line (the generated code carries `#line`
  directives), so the page and the Console say `Lava.hlsl:12: undeclared identifier 'Sped'`.
- **Last good kept.** A failed cook keeps the last good product, as scripts do, so a broken edit
  never makes the mesh vanish; the runtime compiles only what a cook already proved compiles.
- **Shipped as bytecode.** Export cooks each project shader for the preset's formats (SPIR-V,
  DXIL, WGSL through the same naga/tint step) into the dist's pack, beside the engine corpus. A
  dist still carries no compiler, and the web works. A material's shader joins its dependencies,
  so a pruned export keeps it.
- **Hot reload.** Saving recooks and the editor reloads the product (the path materials and
  textures use today); dependent materials follow through the recorded edges. Pipelines swap only
  once the new variant exists.
- **Includes, for 0.1, are the engine's library** (`#include "surface/noise.hlsli"` and similar,
  shipped with the templates); project `.hlsli` files with dependency tracking come after.

## The editor

- **Create > Materials > Lit Surface Shader / Unlit Surface Shader**: a starter `.hlsl` in
  `Sources/`, linked to a new shader asset, opened in its page. Importing a `.hlsl` file makes one
  too.
- **The shader page**, on the script page's pattern: a code editor with an HLSL lexer (the C-like
  lexer already has HLSL's `#` lines; it needs keyword tables), completion for the template's
  names (`SurfaceInput`, `SurfaceOutput`, `Time()` (the view's clock, already in `IblParams.z`),
  the declared properties), errors checked as
  you type (a compile without saving, debounced) shown as markers, a list and a status line, and
  Save to cook. Beside it a live preview: the material page's sphere (or cube, plane, a project
  mesh) drawn with the shader and the properties' defaults.
- **The material page**: the Shader row becomes a picker (the built-in PBR and Unlit, and the
  project's shaders); its rows come from the shader's properties; the preview resolves the shader
  asset (today it ignores it); it refreshes when the asset changes outside the page.
- **Create > Materials > Material from Shader...**, or picking a shader on an existing material.
- **Disassembly** (the backlog's "nicety"): a toggle on the shader page showing the cooked
  SPIR-V or WGSL of a chosen variant. Small, and last.

## Agents (MCP)

- `shader_validate`: compile a source without saving, as `script_validate` does, returning errors
  with lines and the parsed properties.
- Creating a shader is `asset_create` with the new creators; setting a material's shader and its
  values is `asset_data_write` today. A `material_set_shader` tool if the envelope edit proves
  awkward (a finding, fixed in the engine).
- McpGuide gains a Shaders workflow; `kEngineToolCount` is bumped.

## The showcase

In the fifth sample (user, 2026-10-12): the networking and crowds game, which the backlog
already schedules after custom shaders, built through the tools. Plus a user docs page, "Custom
shaders", with the lava example.

## Phases (each a layer with its tests, both compilers; Sedulous ports after each)

Low-level shaders first: the engine already draws them, so each phase carries them one step
closer to an editor user and a shipped game. Surface shaders then build on the same asset path.

0. **The spike, and entry points as data** (done 2026-10-12): `Code/Samples/CustomShader`, the
   toon shader in `Data/Assets/shaders/toon/toon.hlsl`, one file with `VSMain` and `PSMain`, kept
   as the low-level reference sample. Entry names are carried end to end (user, 2026-10-12: what
   is correct over what is convenient): a source registers each stage's entry, the compiler,
   WGSL translation and pack (v3) carry it, every module records it, and a pipeline stage runs
   the entry it names or its module's; no pass writes `main`. Still owed on the way: the built-in
   depth and shadow pipelines' "attribute not consumed" validation warnings.
1. **The shader asset compiles at cook time.** The low-level asset (its stages, its `properties`
   block) cooks every variant for the editor's backend, fails the cook with errors mapped to the
   file and line, keeps the last good product, and carries the property table; a material on it
   takes its layout from that table, and the cook checks the declared set-2 block agrees. A
   failed runtime compile is cached, not retried every frame. Tests: a good shader cooks its
   variants; a broken one fails with its line and the old product stays.
2. **Export.** Project shaders cooked to the preset's formats (SPIR-V, DXIL, WGSL) into the dist
   pack; the player resolves them compiler-free; the material-to-shader dependency survives
   pruning. Tested on a desktop dist and the web build.
3. **Editor.** HLSL lexer; Create > Materials > Shader (the toon as the starter) and a `.hlsl`
   importer; the shader page with live errors and a preview; the material page's shader picker,
   shader-driven rows and a preview that resolves the shader asset.
4. **Agents and docs.** `shader_validate`, McpGuide, the tool count; a user docs page,
   "Custom shaders", on the low-level kind.
5. **The lighting library.** Split `forward.ps.hlsl`'s BRDF, light loops, shadows and IBL into
   `lighting.hlsli` (forward renders pixel-identically). Low-level shaders can include it too, so
   a toon shader need not copy the shadow sampling.
6. **Surface shaders.** The templates around `Surface()` and `Vertex()`, the generated
   declarations with `#line` mapping, generated depth/shadow/pick variants (so displacement and
   cut-outs shadow correctly); the built-in PBR re-expressed as a surface function renders the
   same as `forward`. The editor's creators gain Lit and Unlit Surface Shader; the docs page gains
   the lava example.
7. **The showcase**, in the fifth sample (the networking and crowds game).
8. After 0.1: the disassembly view and project includes; the node graph generating surface
   functions is 0.2.

## Decided

- **Two kinds, both supported** (user, 2026-10-12): low-level shaders and surface shaders.
- **One `.hlsl` per shader, with named entries** (`VSMain`, `PSMain`), and entry names carried as
  data rather than assumed (user, 2026-10-12). Built (phase 0).
- **The order**: low-level shaders through the editor and into dists first (phases 1 to 4), then
  surface shaders (5, 6) (user, 2026-10-12).
- **The properties block** is the single declaration of a shader's parameters, in both kinds; a
  low-level shader also declares its cbuffer, and the cook checks they agree (user, 2026-10-12).
- **The showcase goes in the fifth sample** (user, 2026-10-12), the networking and crowds game.
- **The node graph is 0.2** (user, 2026-10-12; `backlog-0.2.md`).

## Corrections this plan owes other documents

- backlog-0.1.md: "custom shaders already work at the engine level" and "shader include assets"
  are not so (above).
- shaders-materials-hot-reload.md: no signature hash, no interface validation, and the old product
  is not kept on a failed compile.
- shaders.md: user shader assets have no path into a dist today.
