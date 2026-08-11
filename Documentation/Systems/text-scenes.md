# Text Scenes — XML source streams with binary export staging

Status: APPROVED (2026-07-17) — no backward compat required for the scene wire; the test
project gets wiped. Old BINARY source streams still load (the read path survives as the
staged-product decoder), so real projects migrate by open + re-save.

## 1. Problem

The source database is text (XML envelopes) *except* the payload that matters most: the
`"scene"` data stream of scenes and prefabs is `BinarySerializer` output. Scenes are the
most merge-sensitive, most frequently hand-inspected asset in any project, and today they
are opaque to git — undiffable, unmergeable, uneditable. This defeats the point of a text
source DB.

## 2. Decision

- **Scene/prefab source streams become XML**, written by the same `SerializeScene` walking
  the same records through the existing `XmlSerializer` (`draconic.xml.serialization`).
- **Scenes stay NON-cooked assets.** No SceneBuilder, no cook badges. Export staging —
  which ALREADY re-encodes XML envelopes to binary — additionally re-encodes the scene
  stream: the staged pak carries binary, the player is untouched.
- **One representation.** The in-memory `Scene` and the record structure are singular;
  XML and binary are two encoders on one `SerializeScene` path. Edit-while-play operates
  on live scenes and never touches serialized data (SceneSnapshot stays in-memory binary).

Rejected alternatives: (a) text-everywhere — player pays XML parse on scene load;
(b) scenes as cooked assets — pulls scene/component libs into the cooker, adds a builder
and badge machinery for what is a 1:1 re-encode; export staging already owns re-encoding.

## 3. Format

Detection: first non-whitespace byte `<` ⇒ XML; anything else ⇒ binary (existing magic
sniff). Scene streams never legitimately start with `<` in binary (first bytes = magic or
a name length), so the sniff is well-defined.

The XML stream is the same section sequence (header, name, entities, components,
systemSettings, prefab section) with THREE encoding differences, all in the interest of
diffability and text-native skip-unknown:

1. **Component records are INLINE, not blobs.** Binary v2 uses length-prefixed blobs so
   unknown types can be skipped; XML doesn't need that — each record becomes its own
   `<object>` scope (`owner`, `type`, then the component's own Serialize output nested
   via `WriteComponent`). Skip-unknown = pop the scope without reading children. Diffs
   show real fields (`<f32 name="x">1.5</f32>`), not hex.
2. **System-settings records likewise inline** in an `<object>` scope per system.
3. **The stream-position probes don't apply.** XML files are always current-version and
   always carry every section; the legacy probes (`legacyProbe->Tell()`,
   `payload.Tell() >= Size()`) remain binary-only.

Everything else is shared verbatim: header magic/version as serialized values, guids as
canonical 36-char strings (the `Serializer` text default), floats via `std::to_chars`
shortest-round-trip (ALREADY exact — text saves cannot produce noise diffs or phantom
prefab overrides), prefab records via `Write/ReadPrefabRecord`. Their `componentOps`
override payloads serialize INLINE too (8b5b76b): the op's blob deserializes into a
transient scratch entity so its fields write through `WriteComponent` (reads re-blob the
same way — every record site has the scene and its managers). An op whose manager is
unknown at write keeps the hex form (`form=0`) so transcodes never lose it; unknown at
read drops with a warning, matching component-record skip semantics.

## 4. Touch points

- `detail::SceneStreamEncoding { Binary, Text }` + sniff helper; a small read-context
  that owns the parsed `XmlDocument` + serializer storage and hands back `Serializer&`.
  `draconic.scene.resource` gains a dependency on `draconic.xml.serialization` (pure
  core-level lib; the player links it but only ever exercises the binary path).
- `SerializeScene(..., encoding)`: write-side encoding parameter; read-side derived from
  the sniff. Record-encoding branches as §3. Write-side ALSO emits parked pending
  prefab instances (not just live states) so a load→save transcode is lossless without
  resolving/spawning instances.
- Writers switched to XML: editor `SaveScene`/`SavePrefab`, `CapturePrefab`,
  `CaptureInstanceAsTemplate`. (`GenerateModelPrefab` inherits via `CapturePrefab`.)
- Readers sniff: `LoadScene`, `SpawnPrefab`, `RevertPrefabInstance`,
  `RebuildPrefabInstances`, `ComputeInstanceDeltasVsTemplate`. The last one reads
  template component payloads WITHOUT a scene; for XML it deserializes each record
  through a transient scratch entity in the live scene (read → blob → remove) since
  blob extraction needs a manager.
- Export staging: `StageScene` transcodes an XML scene stream to binary via a scratch
  Scene created through the app's SceneSubsystem (full manager set via ISceneAware —
  a hand-listed manager set would silently drop component types). Runs on the main
  thread during export prep; scene streams are structure-only (KBs), so this is
  milliseconds. Binary input passes through untouched.
- `SceneSnapshot` (play-in-editor) and `ComponentTo/FromBlob` baselines stay binary —
  in-memory formats, never on disk as sources.

## 5. Invariants & tests

- **One `SerializeScene` path feeds both encoders.** No separate exporter; divergence can
  only be an encoder bug, and the equivalence test catches it:
  author scene → save XML → load → save binary → load → deep-compare (entities,
  transforms, components, prefab records).
- XML skip-unknown: a component/settings record of an unknown type skips with a warning,
  rest of the scene loads.
- XML prefab payloads spawn (nested records included); scene XML round-trips pendings
  through transcode without a resolver.
- Old binary source streams still load (until re-saved); staged/cooked products stay
  binary forever.

## 6. Follow-ups

- ~~Inline encoding for prefab-record `componentOps` override payloads~~ — DONE (8b5b76b):
  ops serialize their component fields inline via a transient scratch entity; unknown
  types keep the hex form on write / drop with a warning on read (skip-unknown semantics).
- ~~CLI export stages XML verbatim~~ — DONE (815b165): DraconicExport carries the manager
  list (AddAllSceneManagers, lockstep with RegisterAllBuilders) and transcodes like the
  editor; the verbatim fallback remains for robustness and is E2E-tested.
- Optional: pretty formatting knobs (indent width, attribute order) if diff noise shows
  up in practice.
