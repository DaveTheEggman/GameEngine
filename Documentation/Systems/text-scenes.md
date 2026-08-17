# Text Scenes

> Status: CURRENT
> Verified: 2026-08-12 @ 84e710a7
> Track: [[text-scenes]]

Scene + prefab SOURCE streams are XML (diffable, mergeable, hand-editable); staged/cooked products and
in-memory snapshots are binary. One `SerializeScene` path feeds both encoders, so the source DB is
text all the way down while the player still loads binary. Shipped. Old binary source streams still
load (the read path is the staged-product decoder), so projects migrate by open + re-save.

## The model

- **Scene/prefab sources are XML**, written by the same `SerializeScene` walking the same records
  through `foundation.xml.serialization`'s `XmlSerializer`.
- **Scenes stay NON-cooked assets** - no SceneBuilder, no cook badges. Export staging (which already
  re-encodes XML envelopes to binary) additionally re-encodes the scene stream: the staged pak carries
  binary.
- **One representation**: the in-memory `Scene` + record structure are singular; XML and binary are two
  encoders on one path. Edit-while-play operates on live scenes; `SceneSnapshot` stays in-memory binary.

Rejected: text-everywhere (player pays XML parse on load) and scenes-as-cooked-assets (pulls scene/
component libs into the cooker for a 1:1 re-encode export staging already owns).

## Format

`DetectSceneStreamEncoding` sniffs the first non-whitespace byte: `<` => `SceneStreamEncoding::Text`,
else `Binary` (a scene stream never legitimately starts with `<` in binary - first bytes are magic or a
name length). The XML stream is the same section sequence (header, name, entities, components,
systemSettings, prefab section) with three encoding differences:

1. **Component records are INLINE**, not length-prefixed blobs. Each record is its own `<object>` scope
   (`owner`, `type`, then the component's own `WriteComponent` output nested); skip-unknown pops the
   scope without reading children. Diffs show real fields (`<f32 name="x">1.5</f32>`), not hex.
2. **System-settings records likewise inline**, one `<object>` scope per system.
3. **Stream-position probes don't apply** - XML files are always current-version and carry every
   section; the legacy `Tell()` probes stay binary-only.

Shared verbatim: header magic/version, guids as canonical 36-char strings, floats via shortest
round-trip (text saves cannot produce noise diffs or phantom overrides), prefab records. Prefab
`componentOps` override payloads also serialize INLINE (a blob deserializes into a transient scratch
entity so its fields write through `WriteComponent`; reads re-blob the same way). An op whose manager is
unknown at write keeps the hex form (`form=0`) so transcodes never lose it; unknown at read drops with a
warning (matching component-record skip semantics).

## Touch points

- `detail::SceneStreamEncoding` + the sniff + a read-context that owns the parsed `XmlDocument` +
  serializer storage. `foundation.scene.resource` depends on `foundation.xml.serialization` (a
  core-level lib; the player links it but only exercises the binary path).
- `SerializeScene(..., encoding)`: write-side takes the encoding, read-side derives it from the sniff.
  The write side also emits parked pending prefab instances so a load->save transcode is lossless
  without resolving/spawning instances.
- Writers emit XML: editor `SaveScene`/`SavePrefab`, `CapturePrefab`, `CaptureInstanceAsTemplate`.
  Readers sniff: `LoadScene`, `SpawnPrefab`, `RevertPrefabInstance`, `RebuildPrefabInstances`,
  `ComputeInstanceDeltasVsTemplate` (the last reads template payloads without a scene, via a transient
  scratch entity for XML).
- **Export staging** (`Editor.Core/Export` + `Tools.Export`): `StageScene` transcodes an XML scene
  stream to binary via a scratch `Scene` created through the app's `SceneSubsystem` (the full manager
  set via ISceneAware - a hand-listed set would silently drop component types); headless consumers
  (CLI export, MCP scene_validate) get the same full set from `engine::AddAllSceneManagers` in
  `Engine.SceneSurface` - the composition root aggregating the per-domain `Add<Domain>SceneManagers`
  functions the subsystems' own `OnSceneCreated` delegate to (count tripwire `kSceneSystemCount`;
  it replaced Tools.Export's private list, which had drifted and was dropping records). Scene streams
  are structure-only (KBs), so transcode is milliseconds; binary input passes through untouched.

## Invariants

One `SerializeScene` path feeds both encoders (divergence can only be an encoder bug), guarded by the
equivalence test: author -> save XML -> load -> save binary -> load -> deep-compare. XML skip-unknown
loads the rest of the scene past an unknown record; XML prefab payloads spawn (nested included); old
binary sources still load until re-saved; staged/cooked products stay binary forever.

## Deferred

Optional pretty-formatting knobs (indent width, attribute order) if diff noise shows up in practice -
not built (no demand yet).
