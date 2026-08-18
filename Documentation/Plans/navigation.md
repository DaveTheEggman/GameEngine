# Navigation (navmesh + agents)

**Status:** BUILDING (Opus, 2026-08-18). Second of the three parity P0 tracks
(after property-animation.md, before terrain.md). Prior art: both Lumix and
Traktor build on Recast/Detour; Lumix's zone model + Detour crowd is the closer
reference.

### Build progress

- [x] **P0 - vendor** (53d5fe55): recastnavigation v1.6.0 vendored by copy;
  `ThirdParty::Recast` compiles Recast + Detour + DetourCrowd (tile cache +
  debug utils in-tree, uncompiled). Clang + gcc.
- [x] **P1a - foundation.navigation** (ee05fe36): the Recast/Detour core behind
  PIMPL (rc*/dt* confined to NavigationImpl.cpp). Bake (pure, deterministic) +
  NavigationMesh/Query/Crowd. Headless battery both compilers (755 assertions):
  bake determinism + degenerate rejection, path detours an obstacle, disconnected
  reported incomplete + off-mesh reported failure, two agents cross without hard
  overlap.
- [x] **P1b/P2 - resource + pipeline** (45d5cda6): foundation.navigation.resource
  (NavigationZoneSource/NavigationZone/factory) + navigation.pipeline
  (NavigationZoneAsset with the baked blob in a SIDECAR stream, passthrough
  builder) + Pipeline.Registration wiring (kBuilderCount 22 -> 23). Full-chain
  cook + factory + query test both compilers.
- [x] **P3 - engine.navigation** - P3a (dfb80fa0) + P3b (54202969). Components
  (NavMeshZoneComponent + NavAgentComponent, reflected: displayName/category, zone
  Ref picker, agent API `NavAgent.of(entity).navigate/stop/finished/remaining/
  velocity*`); NavigationSceneSystem (per-scene loaded zones, one dtCrowd per zone,
  crowd tick + MoveEntity writeback, zone-local transform); wired into
  Engine.SceneSurface (kSceneSystemCount 34). Runtime injection via
  NavigationSubsystem (ISceneAware) + NavigationZoneFactory in DefaultApp; script
  facade surfaced (kSubsystemFacadeNameCount 32). Engine test both compilers (an
  agent navigates a zone); the Player executable links end to end. Debug draw
  folded into P4 (editor visualization).
- [ ] **P4 - Editor.Navigation**: async "Bake Navigation" action (writes the zone
  asset sidecar) + zone gizmo/extents + debug-draw toggles.
- [ ] **P5 - acceptance**: demo scene (zone + obstacles + 3+ click-to-navigate
  agents via script), wasm target build (gate like Jolt), user visual pass.

Original spec follows.

GOAL: baked navmesh zones + agents that path and avoid each other, usable
from scripts, working headless and on every platform including web.

USER DECISIONS (2026-08-10): zone-based like Lumix; bake source = STATIC
MESH GEOMETRY (foundation.geometry triangles - deliberately not tied to
rendering or physics; Recast eats triangle soup and our StaticMesh is
CPU-readable positions+indices, which keeps the bake headless: scene +
geometry resources only, no physics world, no device). Detour crowd
avoidance IS in P1.

## Module layout (confirmed shape)

- `ThirdParty/recastnavigation` - FULL-tree vendor (house rule), pinned
  release. House target `ThirdParty::Recast` compiling Recast + Detour +
  DetourCrowd only (DebugUtils/DetourTileCache in-tree, not compiled - the
  tile cache joins in P2). Pure exception-free C++ - no -fno-exceptions
  friction expected; verify at bring-up anyway. Compilers: clang + gcc +
  MSVC + Emscripten (gate the wasm build like AngelScript/Jolt; isolate
  any patch as its own commit).
- `Code/Foundation/Navigation` - module `foundation.navigation`, alias
  `Foundation::Navigation`. Wrappers over Detour (NavigationMesh,
  NavigationMeshQuery, NavigationCrowd) + the BAKE over Recast
  (NavigationMeshBuilder: triangle soup + agent/cell params in, serialized
  navmesh data out). No scene/engine dependency - the bake is a pure
  function, testable headless. Recast/Detour headers ONLY in
  implementation units (gcc module hygiene).
- `Code/Foundation/Navigation.Resource` - module
  `foundation.navigation.resource`. The cooked navmesh-zone resource +
  loader (collision-asset precedent: a binary data asset produced by a
  bake action, cooked through the triad).
- `Code/Pipeline/Navigation.Pipeline` - NavigationZoneAsset (Pipeline
  domain) + builder cooking baked data to the product. Bump kBuilderCount
  + tripwire. No OS-file importer (zones are authored in-scene).
- `Code/Engine/Navigation` - module `engine.navigation`. NavigationSubsystem
  (per-scene: loaded zones, one dtCrowd PER ZONE - see Runtime flow),
  NavMeshZoneComponent,
  NavAgentComponent, debug draw. New subsystem checklist: DefaultApp
  registration; out-of-tree script facade (facade rules; never named Game)
  with RegisterNavigationScriptFacade - BUMP kSubsystemFacadeNameCount in
  Engine.ScriptSurface + its tripwire; component displayName + category
  attributes; InspectorView ref-picker entry for the zone's asset Ref.
- `Code/Editor/Editor.Navigation` - bake action (async with progress),
  debug-draw toggles, zone gizmo/extents editing.

## Components + data

- **NavMeshZoneComponent**: zone AABB extents; bake params {cell size, cell
  height, agent radius, agent height, max climb, max walkable slope};
  Ref<NavigationZoneAsset> (the baked data this zone loads at runtime).

INSTANCE MODEL (clarified 2026-08-10): a zone is a REGULAR COMPONENT - an
entity, N per scene, positioned by the entity transform (translation +
Y-rotation, the terrain rule). Zones stay INDEPENDENT navmeshes rather
than tiles of one merged mesh because a navmesh is baked FOR an agent
profile (radius/height/climb are baked in) - independent zones make
per-profile zones natural (overlapping "infantry" and "vehicle" zones
over the same area are legal and useful). Cross-zone pathing is OUT for
P1 (documented limitation). Bake space is ZONE-LOCAL (relative to the
zone entity transform at bake time; queries transform through the current
transform) - this is what lets a level-chunk PREFAB carry its geometry
AND its baked zone to any placement without rebake. Baked data is a
SNAPSHOT: zone or geometry moving independently after bake = stale until
rebake (visible via debug draw; a stale indicator is P2 polish).
- **NavAgentComponent**: radius, height, max speed; runtime API
  (reflected, natural types): Navigate(destination), Stop(), IsFinished(),
  RemainingDistance(). Movement mode flag: MoveEntity (default - the agent
  writes the entity transform from crowd output) vs ReportOnly (exposes
  desired velocity; script/physics moves the entity - the Lumix "move
  entity" toggle, needed for physics-driven characters).
- Bake source collection: walk entities intersecting the zone AABB that
  have a static mesh, transform triangles to world space, feed the
  builder. Per-entity `IncludeInNavigation` flag (default ON for static
  meshes; skinned/dynamic never contribute). Recast's own walkable
  slope/height/climb filtering handles ceiling clutter; a NavBlocker
  volume component (invisible walls/carve-outs) is P2.

## Runtime flow

Scene load -> subsystem loads each zone's cooked navmesh into a dtNavMesh.
ONE dtCrowd PER ZONE (correction: a Detour crowd is constructed against a
single navmesh, so the crowd cannot be per-scene). An agent registers
with the zone whose bounds contain it (overlap tie-break: first match in
P1, explicit assignment is P2); its pathing is bounded by that zone. Tick order: crowd update in the scene update phase,
BEFORE property/skeletal animation and render extraction; MoveEntity
agents write transforms there. Null-scene and pre-scene safety per the
facade battery rules (every facade method guards).

## Editor flow

Zone component gizmo shows extents; "Bake Navigation" action (page
toolbar + component context) runs collection + build on a worker (job
system) with progress; result writes the NavigationZoneAsset source data
+ marks it dirty; normal save/cook stages it (files-are-truth - MCP and
CI can rebake headless later via a tool). Debug draw: navmesh polys,
region boundaries, agent paths + crowd velocities, through the existing
per-scene debug-draw system.

## Tests (required)

- Vendored build gate on all four compilers.
- Bake determinism: fixture triangle soup (a plane with a box obstacle)
  bakes byte-identical data across runs; params change -> data changes.
- Query: path across the fixture avoids the obstacle; unreachable
  destination reports failure (not a crash, not an empty-path success).
- Crowd: two agents crossing paths both arrive (positions advance, no
  NaN, no interpenetration beyond radius tolerance) over a deterministic
  stepped simulation.
- Component wire round-trip (XML scene + cooked), zone asset cook
  round-trip, count guards.
- Headless proof: the whole bake+query battery runs with no render/physics
  subsystem present.
- Tripwires: builder count, facade name count, picker dispatch entry.

## Acceptance

Battery green both compilers; wasm target builds; a demo scene (zone +
obstacles + 3+ agents click-to-navigate via a script) runs in
play-in-editor and export; user visual pass incl. debug-draw overlay.

## Explicitly deferred (P2)

Cross-zone stitching/portals, off-mesh links (jump/ladder), dynamic
obstacles via DetourTileCache + partial tile rebake, NavBlocker volumes,
terrain heightfield as a bake source (lands with the terrain track), MCP
`nav_bake` tool (rides the MCP resume list when useful).
