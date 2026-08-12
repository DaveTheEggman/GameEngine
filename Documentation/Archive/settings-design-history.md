# Settings - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/settings.md
> Track: [[settings-and-export-track]]

NON-AUTHORITATIVE. The Traktor survey + the original settings design (some of which shipped differently).
Present-tense truth is `Systems/settings.md` + `Systems/project-and-settings.md`; the full original doc
is in git at the P0 commit 3b92560d. Kept for the "why".

## Baseline (Traktor) and what we improved

Traktor `Core/Settings`: a dynamic `PropertyGroup` = `SmallMap<wstring, Ref<IPropertyValue>>` with
polymorphic value nodes, accessed by stringly-typed path + templated value type, merged with a recursive
`join()`. We leaned on reflection + the one serializer instead:

| Traktor | Ours |
|---|---|
| Stringly-typed paths at call sites | Typed sections (reflected structs), keyed by type name |
| `Ref<IPropertyValue>` heap object per value | Reflected struct fields (zero per-value heap) |
| No versioning of a group's shape | Versioned payloads per section (`ar.Version()` migration) |
| Structured data is second-class | Structured data is the primary model (`ISerializable`) |
| Its own serialization | The single `ISerializer` stack (binary + XML) |

## What shipped as designed

- Typed reflected sections keyed by type name; `Section<T>()` / `MarkChanged<T>()` / `OnChanged`.
- Whole-section override + struct member-initializer defaults (backward-safe field additions).
- Backend-agnostic via a caller-supplied `SerializerFactory` (XML for hand-editable, binary in tests).
- Unknown-section passthrough on a self-describing (XML) backend; positional binary aborts on an unknown
  section (the same limitation as binary scene records).
- Registration-before-load (an unregistered section type fails the whole load).
- The user-data-dir helper in `foundation.core`, shared with the export templates root.

## What shipped DIFFERENTLY

- **Layering.** The design was ONE `Settings` store with an ordered Default < User < Project LAYER
  STACK (`SettingsLayer`, `LoadLayer`/`SaveLayer`, top-most-layer-wins reads). That did NOT ship. There
  is no `SettingsLayer` enum; layering is achieved by SEPARATE `Settings` store INSTANCES, one per file
  (per-user editor store + per-project editor store + the project manifest), each a flat store. See
  `project-and-settings.md` for the concrete three-store model.
- **Dynamic bag (`PropertyBag` + `SettingsKey<T>`).** Planned as a secondary model for open-ended keys;
  never built, because typed reflected sections covered every consumer. Still a clean future add.
- **Module home.** Planned to live in `draconic.core`; shipped as its own `foundation.settings` module.

## Phased plan (as written)

1. Core primitive (typed sections + Default/User layers + factory load/save + OnChanged) + user-data-dir
   helper. 2. Editor wiring (`EditorExportSettings`). 3. Dynamic bag. 4. Unknown-section passthrough.
   5. Project layer + more editor sections. 6. Settings UI (preferences panel).
