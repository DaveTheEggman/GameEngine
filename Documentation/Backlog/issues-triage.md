# GitHub issues triage (2026-08-08, user-filed)

Nine issues, triaged with code-grounded diagnoses where possible. Cross-refs
into smoketest-fixes.md where they overlap. Same rules as every spec
(CONVENTIONS.md binding; each item lands independently with tests).

## I1. Physics bodies only exist after scene start -> editor debug draw impossible pre-Simulate - CONFIRMED + supersedes smoketest-fixes #4's optimism

Verified: `PhysicsSceneSystem` builds bodies at `OnSceneStarted`, tears down
at `OnSceneStopped` (PhysicsSubsystem.cppm ~5, ~130). So in the editor
BEFORE Simulate there is no world content and `DrawPhysicsDebug` (which
draws FROM the world) has nothing to draw - by construction. The user
additionally reports nothing draws EVEN DURING Simulate, so there are two
distinct fixes:

- **Editor-time collider visualization must draw from COMPONENT DATA, not
  the world** (the way engines draw collider gizmos): a new editor-side
  draw pass over RigidBody/Character/Joint components (shape kind + extents
  + entity transform -> wireframes; no Jolt objects needed). Lives with the
  editor scene page (or an editor-only system), keyed to the viewport
  show-flag proposed in smoketest-fixes #4. This works in ALL modes,
  including pre-Simulate - which is when authoring actually needs it.
- **The Simulate-path bug is real and still unfound**: PhysicsPlayground
  draws fine (user-confirmed) - the break is editor-only. PRIME SUSPECT is
  frame ORDERING: the playground's DefaultApplication loop runs subsystem
  updates (physics submits its DebugScene draws) and THEN renders, while
  the editor renders on its own UI-driven schedule (ViewportView
  RenderContent) - if the editor's composite runs before the physics
  submission (or after the per-frame debug clear), the draws exist but are
  never consumed. Editor gizmos still work because the page submits those
  itself, timed with its own render. Second suspect: the scene-settings
  debugDraw flag not reaching the editor's scene instance. The
  extract-level test from smoketest-fixes #4 decides (Simulate with
  debugDraw ON -> assert the editor-path extract carries the capsule
  draws); fix whatever it finds.

## I2. Scene view mouse capture never released - WATCH (one occurrence, hard repro)

Not diagnosable from one occurrence. Action now: add capture/release
instrumentation - the viewport input surface logs (debug level) every
capture acquire/release with the reason, and an ASSERT-level log if a
capture survives N frames with no mouse button down (the leak signature).
Cheap, permanent, and the next occurrence pins the path. Goes in the
stragglers watch list, not a fix queue.

## I3. Prefab pages: scene settings + Level script visibility - DESIGN DECISION (user second-guessed; here is the resolution)

The user's second thought is right that scene settings are USEFUL while
editing a prefab (preview how it behaves when instantiated). The clean
resolution separates PERSISTENCE from VISIBILITY:

- ALL scene-settings sections (INCLUDING the Level script slot - user
  decision, revising the earlier hide-it lean) stay visible in prefab
  pages as EDITOR-PREVIEW state: edits affect the page's preview scene
  only and are NEVER serialized into the prefab wire (a prefab
  instantiated into a real scene inherits THAT scene's settings). One
  clear banner over the whole settings block: "Preview only - not saved
  with the prefab". The Level slot under this framing is a preview TOOL:
  attach a test scene script and Simulate to watch it drive/spawn the
  prefab - no trap, because nothing here pretends to persist.
- PERSIST the preview settings as per-asset EDITOR-side state (the
  material-page preview-shape precedent - sticks per asset, lives in
  editor state, never in the wire) so closing/reopening the prefab page
  resumes the same test setup. The no-editor-data-in-runtime rule is the
  boundary: editor state file, never the prefab.
- The user's "separate view settings for the editor viewport" instinct is
  the show-flags direction already in motion (Post + the Physics flag from
  smoketest-fixes #4) - per-viewport editor-side toggles, distinct from
  authored scene settings. No new mechanism needed; keep extending
  show-flags.
- Tests: prefab save/load round-trip proves preview-edited settings do NOT
  land in the wire; the section-visibility rules unit-tested where the
  page composes its sections.

## I4. Memory: closed scene keeps memory; project open balloons - TWO ITEMS

- **(a) Scene close does not release resources - REAL GAP.** The
  ResourceManager cache holds every product forever (handles cached by id,
  never evicted - by design for reuse, but nothing ever frees). Fix in two
  steps: (1) INSTRUMENT first - a memory report (Project > diagnostics or
  a console command) listing live products by type with sizes, so "what is
  holding it" is answerable (composes with I5's allocator tagging); (2)
  EVICTION: on scene close, products whose handles have no outside refs
  (proxy refcount == cache-only) are released through the existing
  graveyard/retire discipline (GPU objects respect frames-in-flight). An
  explicit "Purge unused resources" action lands first (safe, user-driven),
  automatic on-close eviction after it proves out.
- **(b) Project open eager cost - PARTLY EXPECTED, verify the boundary.**
  Opening scans the content DB (instance headers - cheap) but must NOT
  decode products or import sources until something binds them. Verify with
  (a)'s report what is actually resident after a cold project open;
  thumbnails and auto-cook checks are the likely legitimate consumers. Fix
  anything found decoding full products at open; document the rest as
  expected.

## I5. Allocator plumbing: everything uses DefaultAllocator at call site - ARCHITECTURAL TRACK (own pace)

True today (DefaultAllocator() at ~every construction site). This is a
code-standard-cleanup-scale track, not a bug fix:
- Phase it exactly like the code-standard cleanup: Core-up, module by
  module; each system/subsystem takes an `IAllocator&` at construction -
  REQUIRED, NOT DEFAULTED (user directive: failure must be loud and fixed
  immediately; a DefaultAllocator() fallback parameter would just relocate
  the ambient-allocator problem one level up). Each module phase is
  therefore a compile-breaking sweep: the signature changes and every call
  site in that phase is updated EXPLICITLY in the same commit, passing a
  real allocator decision down from its owner.
- The PAYOFF making it worth doing now rather than someday: per-system
  tagged allocators feed I4's memory accounting (who owns what becomes a
  report, not a hunt).
- Do NOT thread allocators through hot per-frame paths that never allocate
  steady-state (rings, pools) beyond their construction.
- Each module phase: behavior-identical, both compilers, ASAN green; the
  tag report grows a row per converted system.

## I6. Texture compression missing - FEATURE TRACK (import + cook)

Real gap: imported textures stay raw RGBA; cooking does no platform
processing. Track shape:
- P1: BCn encode at IMPORT for desktop (BC1 opaque / BC3 alpha / BC5
  normals / BC7 quality knob) stored in the cooked texture product;
  TextureResource already carries format - the GPU factory uploads
  compressed as-is. NEEDS A THIRD-PARTY ENCODER (bc7enc/ispc_texcomp
  class) - USER APPROVAL REQUIRED before adding the dependency (vendored,
  per the SDL precedent).
- P2: cook-time PLATFORM axis: desktop keeps BCn; Web preset transcodes
  per the earlier decision (desktop-browser BC ok; mobile ASTC/ETC2 is the
  export-knob future noted in the web track).
- Import dialog gains the compression choice (+ "None" escape); existing
  projects re-import on demand, no forced migration.
- Tests: encode round-trip (PSNR floor per format), cooked-product format
  assertions, player renders a BC-cooked texture (backend probe).

## I7. Audio clip page: pause + volume; runtime volume API - SMALL

- Clip audition page gains Pause (toggles with Play) and a volume slider
  (audition-local gain, not persisted).
- The ACTUAL ask (clarified): a volume control for the EDITOR's embedded
  runtime audio - editing/previewing with game audio blasting needs an
  editor-side knob. Add a master-volume control for the embedded runtime's
  audio engine surfaced in the editor (Game tab toolbar or Preferences,
  persisted as EDITOR state - never project data), applied to the
  engine-level master gain so it covers Simulate, Game tab, and audition
  alike.
- Secondary (only if absent, since it falls out of the same engine knob):
  expose `Audio.setMasterVolume` on the script facade. Battery case if
  added; skip if the bus layout already gives games the control they need.

## I8. Exported player colors wrong (Linux) - ROOT CAUSE UNCONFIRMED (user retesting); harden the swapchain fallback regardless

`VkSwapChainImpl::chooseSurfaceFormat` (VkSwapChain.cppm ~200) falls back
desired -> B8G8R8A8_SRGB -> **B8G8R8A8_UNORM -> fmts[0]** with NO
logging. If the dist machine's surface (different WSI/driver than the dev
box) lacks the sRGB format, the player silently renders into a UNORM
target while the pipeline assumes encode-on-write -> washed output. The
user flags this may NOT be the reported issue (retest pending) - but the
silent fallback is a real hazard on its own and the logging directly
answers the issue's own question ("what is the swapchain format?"). Do
regardless of the retest outcome:
- LOG the negotiated surface format at startup (both the request and the
  result) - turns the next report into a one-line diagnosis.
- On a non-sRGB fallback, the final pass must encode manually (the tonemap
  already has an encode path variant on web - reuse the decision point) OR
  the fallback is refused with a clear error if manual encode is
  impractical on that path. Prefer adapt-and-log.
- The user retests the color issue on the affected machine WITH the format
  log in hand and reports; if the log shows an sRGB swapchain, the hunt
  moves elsewhere (tonemap output path, exported-content color space) with
  the fallback hazard closed either way.

## I10. Windows OpenPathInFileManager: normalize separators - TRIVIAL, exact fix known

The user pinned it: forward-slash paths reach `explorer.exe "<path>"` and
Explorer refuses them. In the Win32 backend, before building the command:
copy the path and flip every '/' to '\\' (bounded by the existing 4096
buffer; reject overlong instead of truncating mid-path). Test: unit-test
the normalization helper (mixed separators, UNC-ish inputs, overlong);
the launch itself stays manual-verify on Windows.

## Suggested order

I10 (trivial) -> I8 hardening (log + adapt; arms the user's retest) -> I1 +
smoketest-fixes #4 together (one physics-debug story) -> I3 (design, small)
-> I7 -> I4a instrument -> I2 instrumentation -> then the tracks: I4
eviction, I6 compression (after dependency approval), I5 allocators
(paced, loud-breakage sweeps). (MSVC setup removed from this doc - the user
runs that separately on Windows.)
