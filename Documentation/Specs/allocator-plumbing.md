# Allocator plumbing - retiring the ambient DefaultAllocator()

> Issue I5's architectural track, started 2026-09-02 (user go). ~3,700
> `DefaultAllocator()` call sites move to EXPLICIT allocators threaded from
> owners. Ground rules from the I5 triage entry + the user's directive:
> **`IAllocator&` parameters are REQUIRED, NEVER DEFAULTED** - a defaulted
> fallback just relocates the ambient-allocator problem one level up, and a
> missing decision must fail at compile time, loudly. Each phase is a
> compile-breaking sweep: the signature changes and EVERY call site in that
> phase updates in the same commit, passing a real allocator decision down
> from its owner. No compatibility shims, no transitional defaults.

## The model

One OWNERSHIP TREE of allocators, rooted at the process:

1. **Root**: the executable's `main` creates the root allocator (the
   `SystemAllocator`, optionally wrapped in tracking for tools/tests) and
   hands it to `foundation.runtime::Context` - whose defaulted ctor param is
   removed in the Core phase. `Context::Allocator()` is the runtime's
   allocator authority.
2. **Systems/subsystems**: take `IAllocator&` at construction (required) and
   typically wrap it in a **`TaggedAllocator`** (`{parent,
   RegisterMemoryTag("Navigation")}`) - one tag per system, from Core's
   existing open MemoryTag registry. `MemoryTagReport()` is the I4 memory
   report's data source ("who owns what" becomes a report, not a hunt).
3. **RefCounted objects**: `MakeRef(allocator, ...)` already records the
   allocator in the control block. Phase 0 adds
   `RefCounted::MemoryAllocator()`, which unlocks the INHERITANCE IDIOM:
   code inside a ref-counted object allocates children from ITS OWN
   allocator (`MakeRef<Label>(MemoryAllocator())`), so whoever created the
   root of a tree decided for the whole tree. This is how the ~2,000 UI
   view-construction sites convert without threading a parameter through
   every control body.
4. **Containers as members of long-lived stores**: constructed with the
   owner's allocator in the init list (`m_items(m_alloc)`).

## Deliberate bounds (what does NOT convert)

- **Plain value structs** (components, math, wire structs) keep
  default-constructed containers. Components live in manager pools; their
  spill allocations attribute to the manager's tag when the manager phase
  lands - per-field threading through POD-ish value types is churn without
  ownership meaning.
- **Hot per-frame paths that never allocate steady-state** (rings, pools,
  scratch buffers) thread the allocator at CONSTRUCTION only (I5 rule).
- **Local/transient containers** in free functions with no owning system
  stay on the default until their module's phase decides an owner. The end
  state for `DefaultAllocator()` is: referenced ONLY at true composition
  roots (executable mains, test mains) and inside the value-struct bound
  above.
- **Tests** construct their own root (tracking) allocator in TestMain and
  pass it explicitly - which also turns every suite into a leak check.
- **Fixed function-pointer ABIs measured during P2** stay unthreaded until
  their owning phase (or permanently, if the owner never materializes):
  the reflection container thunks (`createElement` and friends - shape is
  `(Instance&, usize, TypeInfo&)` across the whole reflected ecosystem),
  the `SerializableFactory` create-by-name path (deserialized object graphs
  - the ContentDatabase seam can add an allocator overload in P3 if
  worthwhile), and the `SerializerFactory` lambda the content DB stores.
  `Thread`'s closure box is a construction-time transient of a value-shaped
  primitive - same family as `Function`'s defaulted parameter (the value
  bound).

## Phase plan (Core-up, the code-standard-cleanup discipline)

Every phase: behavior-identical, both compilers, full battery, ASAN suite
green, and the tag report grows a row per converted system. One module (or
tight cluster) per commit.

- **P0 - enablers** (this session): `RefCounted::MemoryAllocator()`; extend
  Core's existing `:memory_tag` seam (`TaggedAllocator` + MemoryTag registry
  were already in place) with peak bytes, total allocations, and
  `MemoryTagReport()` rows; Context keeps its allocator but gains no
  default-removal yet (that lands with its phase's sweep); tests.
- **P1 - exemplar** (this session): foundation.navigation +
  engine.navigation converted end to end (mesh/query/crowd ctors take
  `IAllocator&`; the scene system owns a tagged "navigation" allocator).
  Small (5 sites) but exercises every idiom; the template for all later
  phases.
- **P2 - Foundation/Core consumers** (DONE): `Context` ctor default REMOVED
  (the first compile-breaking sweep - hosts, runners, editor embed, ~40 test
  sites all made explicit); `ApplicationHost` takes the entry point's
  allocator and threads it into its Context; `JobSystem` takes a required
  allocator backing its worker deques AND every job closure box (leak-checked
  per pool in tests); `RingLogSink` default removed; `Array::Allocator()`
  accessor added (owners thread "allocate like my container" decisions - the
  tiled nav bake's scoped pool now shares its output blob's allocator).
  Registries/reflection builders measured and BOUNDED instead (see the
  deliberate bounds above).
- **P3 - Foundation services**: Xml, Settings, Content, VFS, Http, Net*,
  Audio, Fonts*, Image/Texture/Geometry resource stacks. Each service
  object takes `IAllocator&`; owners thread from Context or their own
  owner. Lands as sub-sweeps: **P3a data backbone part 1 DONE** (XML DOM -
  every node stores its creating allocator, the document's decision covers
  the whole tree, detached subtrees keep their creator's; XmlSerializer
  write mode takes the allocator backing its document; Settings store;
  HttpServer + McpHttpHost). **P3a part 2 DONE**: the ContentDatabase /
  ResourceManager / NativeFileSystem trio - required allocators back
  group/instance nodes, resource handles + pending async loads, and file
  streams/change sources; ~150 construction sites made explicit
  (overwhelmingly test roots). **P3b fonts DONE**: parse/bake APIs take the caller's
  `IAllocator&` (the returned font/atlas/BakedFontData is freed through the
  same allocator - `CachedFont`/`BakedFontData` record it); FontManager /
  TrueTypeFontService / ResourceFontService / FontFactory take required
  allocators; `ExpandR8ToRGBA8` allocates from a passed allocator. The
  global parser/baker slots (TrueTypeFonts / DFFonts / factory Shutdown)
  stay process-root pairs by design. **P3c audio DONE**: AudioEngine takes a required
  allocator; its Impl threads it through every voice/bus/effect-node/scene-
  group/reverb allocation, miniaudio VFS bridge files record theirs, and the
  three audio resource factories follow the required-allocator factory
  pattern (DefaultApp creates them from the runtime Context's allocator).
  **P3d net DONE**: HostServer / JoinServer /
  StartNetworking take the caller's allocator (sockets + managers ride it);
  WebSocketServerGateway / ClientSocket / HybridSocket take required
  allocators (per-client handshake parsers from the gateway's);
  SimDatagramNetwork backs its simulated sockets from a required allocator.
  **P3e script backends DONE**: the backend
  registry's create hook takes `IAllocator&`
  (`CreateScriptManagerForLanguage/ForFile` thread the caller's); both
  backends' entry factories take it and every context/object/blob/delegate
  INHERITS the manager tree's allocator via `MemoryAllocator()` - the first
  large-scale use of the inheritance idiom; `ScriptRunHost` roots its run's
  tree with a required allocator; the AS BoxedVariant thunk boxes are a
  documented process-scope bound (captureless engine callbacks).
  **P3f resource factories DONE**: StaticMesh/
  SkinnedMesh/Skeleton/AnimationClip/AnimationGraph/ScriptClass/Terrain/
  SplatWeights/Heightfield/Texture factories take required allocators;
  DefaultApp creates them all from the runtime Context's allocator; the
  cooked-record Build helpers (Heightfield::Build, SplatWeightsSource::
  Build, MigrateLegacySplatmap) allocate their products from a passed
  allocator. Remaining P3 misc: Scene.Resource internals, Physics,
  Shell.Desktop, Shaders.System, Xml.Serialization bound tail - fold into
  P5/P6 or a small mop-up slice.
- **P4 - UI cluster** (the bulk: ~1,000 first-party sites + tests): the
  inheritance idiom does the heavy lifting - `UIContext`/`RootView` carry
  the tree's allocator; control bodies switch `DefaultAllocator()` ->
  `MemoryAllocator()`; hosts (UIHost, editor app, samples) decide roots.
  Toolkit + Gamekit ride the same sweep.
- **P5 - Engine subsystems**: render/physics/animation/terrain/particles/
  scene managers - each subsystem tags; scene systems receive the scene's
  allocator (Scene itself converts here, owning a per-scene tag).
- **P6 - Pipeline + Editor + Samples + remaining tests**: importers/builders
  take the cook's allocator; editor app threads its root through pages/
  views (mostly free via the UI idiom); samples construct explicit roots.
- **P7 - the lockdown**: `DefaultAllocator()` audited to composition roots
  only; a grep tripwire test pins the allowed-file list so regressions fail
  CI.

## Sweep protocol (every phase)

1. Change the signatures (required `IAllocator&`, stored as `IAllocator*`
   member or wrapped in a member `TaggedAllocator`).
2. Fix every caller IN THE SAME COMMIT with a real decision (owner's
   allocator, never a re-introduced default).
3. Long-lived member containers pick up the owner's allocator in init
   lists.
4. Both compilers + battery + the ASAN suite; `MemoryTagReport()` must show
   the new tag with plausible numbers.

## Report seam (feeds I4 + the memory-profiling seed)

`MemoryTagReport(Array<MemoryTagReportRow>&)` - rows of
{tag, name, liveBytes, peakBytes, liveAllocations, totalAllocations}. The editor
diagnostics panel / MCP report consume this; the memory-profiling weekly
seed's "tagged allocators" piece is THIS, delivered incrementally as phases
land.
