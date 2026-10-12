# 0.1 backlog - open items for the 0.1 release

> The work queued for the engine's 0.1 release, in three parts: the editor, the engine, and the
> rest of the release (builds, samples, docs, the website and the flagship game). Moved here from
> `weekly_backlog.md` on 2026-10-10; that file keeps everything else open, and `backlog-0.2.md`
> what waits for 0.2. Pull an item out of here into the current weekly when it is started; delete
> it here when done.

## Editor

- **An orthographic editor camera** (user 2026-10-09; big, user 2026-10-11: several rendering
  effects are disabled for orthographic cameras today, and they come first): the scene view's camera can switch
  between perspective and orthographic with a toggle (in the view's toolbar, and a key). Pairs
  with the orientation gizmo: snapping to a face is where an orthographic top, front or side
  view is most used. The engine's cameras already have an orthographic projection
  (`MakeCameraProjection`); the editor camera's zoom becomes the ortho size there.
- **Project-wide defaults for scene settings, per domain** (user 2026-10-09, to think about): a
  scene's render settings come from the scene itself (its own values, or a profile asset it
  names, as Lamplight's levels share theirs), else from hard-coded defaults; other domains are
  probably the same. Add a project layer between: each domain's settings with project-wide
  defaults, set in the project settings, so the lookup goes scene, then project, then the
  hard-coded default. Survey which domains have scene settings, and how a project default
  relates to the profiles (a project default could simply name a profile).
- **Custom shaders through the editor, before the 0.1 release** (user 2026-10-09, ready and
  showcased for 0.1; merged 2026-10-10 with the planning seed of 2026-08-26, "in-editor shader
  editing + material/shader node-graph editor"). PLAN: `Specs/custom-shaders.md` (2026-10-12):
  two kinds, low-level shaders (proven at the engine level by `Samples/CustomShader`, the toon)
  and surface shaders on top; one `.hlsl` per shader with named entries (`VSMain`/`PSMain`;
  entry points are carried as data, user 2026-10-12, built); the node graph moved to 0.2. (The
  original note said "shader include assets" exist: none does; includes resolve against
  `Data/Shaders` in dev builds.) Expose the pipeline in
  the editor (author a shader asset, a material that uses it, see it cook, compile and draw, with
  its errors shown), and showcase it: a sample project or a demo game's effect built on a custom
  shader.
  - **Origin of the seed**: the parity doc, "MATERIALS: par - both shader-ref + property-set with
    editor pages, NO node graph either side" and "SHADERS: ... Lumix-ahead on one nicety:
    in-editor shader editor WITH LIVE DISASSEMBLY."
  - **What it builds on**: the offline shader cook to a versioned SPIR-V / DXIL / WGSL pack with
    a variant lint (so "live disassembly" is our cook output, already produced); `CodeEditView`
    and its per-language lexer registry (a shader code page with an HLSL lexer, save re-cooks and
    hot-reloads); `MaterialEditorPage` (shader ref, property set, preview), the page a graph would
    feed; the `NodeGraph` UI and `AnimationGraphPage` (three panes, persisted node positions) as
    the precedent for a graph editor; custom materials without renderer changes.
  - **Two tiers to weigh and sequence**:
    1. In-editor shader source editing, the cheaper win: open a shader in a `CodeEditView` page
       with the HLSL lexer; saving re-runs the cook and hot-reloads, and the page shows the cooked
       SPIR-V / WGSL disassembly (from our cooked pack, not a runtime compiler).
    2. A material / shader node graph, the big feature (neither engine compared has one): visual
       nodes (texture sample, math, blend, ... to a surface output) that generate HLSL fed through
       the existing cook, so the variant lint and the compiler-free dist still apply. Authors
       custom materials without hand-written shaders, on the NodeGraph UI and the
       AnimationGraphPage pattern.
  - **The design doc decides** (in `Documentation/Specs/`, before any graph code): tier 1 first
    with tier 2 as the follow-on, or the graph directly; the graph's data model (nodes to
    generated HLSL, how variants and features interact, the material property surface it
    exposes); how generated shaders ride the variant cook and lint; the graph's serialization and
    editor shape (reusing AnimationGraphPage's spine). For 0.1, tier 1 and the showcase; the graph
    is 0.2 (user 2026-10-12, `backlog-0.2.md`).
- **Vertex snap, the next steps** (from the seeded item, 2026-08-26; the pivot-onto-vertex snap
  is built, week 2026-10-10): pick a SOURCE vertex on the moved object (Blender-style, any corner
  as the anchor, not only the pivot); snap to edges, faces and the grid as well as vertices; a
  skinned mesh in its current pose (today its bind pose, the pose its CPU vertices hold); the
  instanced meshes and other drawn surfaces answering `ISceneSurfaceQuery` too.

## Engine

- **A script sets a scene's sky texture and environment profile** (user 2026-10-10): no script
  can point a scene at another sky texture or Environment Profile. `EnvironmentSettings.of(scene)`
  leaves its asset references out of AngelScript (the property loop skips a type it cannot declare)
  and in Luau takes only another bound `Ref` of the same type, a plain copy that nothing
  re-resolves (`EnvironmentSystem::ResolveResources` runs only from ResolveSceneResources and the
  editor); `UseSettingsProfile` is not on the script surface. Give scripts what
  `SceneRender.setMesh`/`setMaterial` already do for components: set by asset (guid or a typed
  asset handle) and bind at once, the sky's environment rebaked; switching the profile turns the
  block's source and binds the profile. Mind that the handle resolves to `Effective()`, so in
  Profile mode a write lands in the shared profile. Then tell Sedulous (it copies the value fields
  and leaves its references untouched today).
- **Luau's write error for a mistyped property says "read-only"** (user 2026-10-10): assigning a
  value of the wrong type to a reflected property (a guid or a string to a `Ref` field, say)
  fails in `SetProperty` as InvalidArgument, and Luau's `__newindex` reports every failure as
  "property is read-only" (`LuauScript.cppm`, NewIndexThunk). Say what failed: the property takes
  a value of type X, not Y; keep "read-only" for a property that has no setter.

- **Players rebind their controls** (user 2026-10-12; found writing the input docs): a player's
  rebinding has a home, `InputBindingOverrides` in the user settings (`Foundation/Input/
  InputMap.cppm`), but nothing outside its tests reads or writes it, so a game cannot offer
  rebinding. Apply the overrides when the input map binds at startup, and give scripts what a
  controls menu needs: list an action's bindings with their display names, listen for the next
  input to rebind one, reset to the map's own, and save. Then the input docs page gains a section.
- **Scripts receive network messages** (user 2026-10-12; found writing the networking docs):
  `Net.rpc`/`rpcNumber`/`rpcText` send, but nothing receives: `RpcTable::On` is registered only
  in tests, and networking.md calls the receive side (`Net.on(name, fn)`) P3 slice 1 of the
  commands plan (`Plans/networking-commands.md`). Build that slice, with its number and text
  payloads, on both languages; the fifth sample needs it. The networking docs page's "What is
  missing" list then loses its first line.

- **Scene facades as scene properties, as Sedulous has them? Decide before 0.1** (user
  2026-10-09): Sedulous binds a scene facade as a property of the scene (`scene.Physics.RayCast`,
  its SceneFacade role); ours are reached through the facade type (`ScenePhysics::of(scene)`,
  `SceneRender::of`, `SceneAudio::of`, ...), kept so by the ruling of 2026-10-01 until the role
  was revisited. Look at it before 0.1, while the script surface can still change freely: which
  reads more naturally in AngelScript and Luau, what moving means for every facade, the sample
  games' scripts and the docs. The earlier write-up is `Plans/beef-script-facades-note.md`.
- **Luau variants of the sample games, and whether we keep two languages** (user 2026-10-09):
  perhaps write a Luau version of each sample game's scripts (all four are AngelScript), as an
  exercise of the Luau side end to end either way. First decide whether we really want to
  support two scripting languages (the cost of every script facade, test and doc twice, against
  what each brings). The heist plan already had a Luau-only game coming after Lamplight.

### Seeded: GPU profiler + profiler visualization (user 2026-08-26)

Origin: same parity doc - "GPU PROFILER: Lumix-ahead - unified CPU/GPU profiler
timeline + pipeline statistics + NVML telemetry + PIX/RenderDoc; Draconic:
per-pass graph timestamps + CPU record-time report, SEPARATE from its CPU
profiler." We have both halves but they are text-dump only and disjoint.

What exists ([[profiler-system]]): `draconic.profiler` CPU scopes
(compile-gated) + a GPU `GraphProfiler` (per-pass Vulkan timestamps); the P key
DUMPS TEXT. Two gaps, and the user asked for both:

1. **GPU profiler** - broaden past the current per-pass Vulkan-only timestamps:
   finer scopes and coverage on the OTHER backends (WebGPU timestamp-query where
   available, DX12) so the GPU timeline is not Vulkan-only. Pipeline statistics
   (draw/primitive counts) alongside the timings if cheap.
2. **Profiler visualization** - replace the text dump with a VISUAL profiler
   panel (timeline / flamegraph) that UNIFIES the CPU scopes and the GPU
   timestamps on ONE per-frame timeline (the "separate" problem the parity note
   calls out). ImGui is already integrated ([[imgui-integration]]) - a candidate
   host for the overlay - but decide editor-panel vs in-app-overlay at design.

Open questions for week start:
- The shared data model: merge CPU scope trees + GPU pass timings into one frame
  record with a common clock/axis (GPU timestamps resolve a frame or two late -
  the view must align them to the right CPU frame).
- Where it lives: an editor profiler panel, an ImGui overlay in the samples, or
  both reading the same capture. Keep the capture layer UI-free.
- Portability: WebGPU timestamp-query is optional/limited and DX12 differs from
  Vulkan - the GPU side degrades gracefully where timestamps are unavailable.
- Compile-gating stays (the CPU profiler is already gated); the visualization is
  a debug tool, not shipped in dist.

## Other

The release itself and the work around it: builds and packaging, the samples, the docs, the
website and the flagship game.

- **Documentation cleaned up and made proper, for the 0.1 release** (user 2026-10-09): the
  documentation as a user of the engine meets it, current and organised (getting started, the
  editor, each system, scripting, exporting, the MCP guide). Ruled 2026-10-11: it is written
  fresh in its own repository, `AssiduousEngine/docs` (Astro Starlight, private source, published
  to the public `docs-site` at https://assiduousengine.github.io/docs-site/), from the DeepWiki
  page, corrected against `Documentation/` and the source; this repository's `Documentation/`
  stays as the engine's development documentation. Started 2026-10-11: the home page and the
  introduction, with the outline in its README.
- **The 0.1 release** (user 2026-10-09): the release itself, with
  - **a website** for the engine: started 2026-10-11 as `AssiduousEngine/website` (Astro, private
    source, published to the public `assiduousengine.github.io`, live at
    https://assiduousengine.github.io/): a landing page and the sample games, whose web builds
    publish apart to the public `AssiduousEngine/demos` at `/demos/` (the website's
    `scripts/publish-demos.sh`), replacing the old GameEngineDemos site (user 2026-10-11); the
    domains are the user's, their DNS not yet set up;
  - **a showcase game**: the engine's flagship, released on Steam. Separate from the sample
    games in this repository and not part of it (its own project and repository); the samples
    stay the engine's examples.
  The items marked for 0.1 above (custom shaders through the editor, the documentation, the
  fifth sample below) belong to it.
- **A fifth sample game, on networking and crowds, before 0.1** (user 2026-10-09; after custom
  shaders, which it showcases too, user 2026-10-11): no sample
  project uses the network stack or crowds yet, so neither has been showcased or stressed in a
  game. The fifth sample in `Data/SampleProjects/` built around both: players over the network
  (the engine's reliable UDP and replication) and a crowd (instanced skinning, navigation
  crowds), built and playtested through the MCP tools as the others were. (Lamplight's crowd
  was set aside "for later" when it was planned.)
- **The 0.1 release builds and samples** (user 2026-10-09; after every other 0.1 item but the
  release notes and download links, user 2026-10-11): build and package what users
  download for 0.1: the export templates (each platform), the editor and the command-line tools
  (Tools.Mcp, Tools.Export, Tools.Cook and the rest a user needs; `scripts/build-editor-dist.*`
  and `scripts/build-export-templates.*` are the starting point), and the sample projects
  packaged so users have something to open and play with. Perhaps the project launcher lists
  remote samples too, to download and open from there (pairs with the launcher polish above).
