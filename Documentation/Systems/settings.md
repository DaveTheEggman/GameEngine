# Draconic — Settings system (design)

Plan of record for an engine settings system: typed, versioned, layered, persisted through the
existing serialization stack. First consumer is **editor settings** (the export templates root, then
recent projects, window layout, theme, tool prefs); usable engine-wide (runtime/game settings later).

Status: **design** (2026-07-15). Precursor to export Phase 4 (§11.1 of `docs/design/export.md`).

---

## 1. Goals

- A home for editor settings: **configurable templates root** first, then recent projects, panel
  layout, theme choice, per-tool prefs, …
- **Extensible** — any subsystem/plugin contributes its own settings without editing a central schema.
- **Persisted** human-readably through the **existing `ISerializer` + versioned-payload** stack (XML
  backend), so settings migrate across engine versions like every other Draconic format.
- **Layered** — built-in defaults < user (global) < project (local); the higher layer wins.
- Robust to **unknown sections** — settings written by a build with extra sections survive a round-trip
  through a build that lacks them (no silent loss).
- An **owned service**, injected — not a hidden global; unit-testable headless.

---

## 2. Baseline (Traktor) and what we improve

Traktor `Core/Settings`: a dynamic `PropertyGroup` = `SmallMap<wstring, Ref<IPropertyValue>>` with
polymorphic value nodes (`PropertyBoolean/Integer/Float/String/StringArray/Color/Object/...`), accessed
by stringly-typed path + templated value type, merged with a recursive `join()`.

Draconic does better by leaning on reflection + the one serializer:

| Traktor | Draconic |
|---|---|
| Stringly-typed paths scattered at call sites | **Typed sections** (reflected structs) + `SettingsKey<T>` binding name+type+default **once** |
| `Ref<IPropertyValue>` — a heap object per value | Reflected struct fields (zero per-value heap); a compact tagged `Value` for the dynamic bag |
| No versioning of a property group's shape | **Versioned payloads** per section — real migration (`ar.Version()`) |
| Structured data is a second-class `PropertyObject` | Structured data is the **primary** model (an `ISerializable` struct) |
| Its own serialization | The **single** `ISerializer` stack (binary + XML), same as scenes/manifests |

---

## 3. Architecture

Two complementary models under one `Settings` store. Prefer typed sections; reach for the dynamic bag
only when a schema is genuinely unknown ahead of time.

### 3.1 Typed sections (primary)
A settings section is a reflected `ISerializable` struct with a data version. A subsystem just declares:
```cpp
struct EditorExportSettings : ISerializable {
    DRACONIC_OBJECT(EditorExportSettings, ISerializable)
    String templatesRoot;                 // "" => platform default (see export §5)
    void Serialize(ISerializer& ar) override { core::Serialize(ar, "templatesRoot", templatesRoot); }
};
DRACONIC_DEFINE_OBJECT_VERSIONED(EditorExportSettings, "draconic::editor", 1)
```
Access is typed and keyed by the reflected type name — no path strings:
```cpp
EditorExportSettings& s = settings.Section<EditorExportSettings>();   // lazily created/loaded
s.templatesRoot = pickedDir;
settings.MarkChanged<EditorExportSettings>();                          // fires notifications, flags dirty
```
Typed, versioned, migratable, and free of stringly-typed keys.

### 3.2 Dynamic bag (for open-ended / plugin cases)
A built-in `PropertyBag` — itself a section type — for ad-hoc key/values where a struct is overkill:
- `PropertyBag` = `Map<String, Value>`, where **`Value`** is a compact tagged variant
  (`Bool | Int(i64) | Float(f64) | Str(String) | StrArray`) — **not** a heap object per entry.
- Keys are defined **once** as typed handles, so they are centralized, not scattered:
  ```cpp
  inline constexpr SettingsKey<bool> kShowGrid{ u8"viewport.showGrid", true };
  bag.Get(kShowGrid);         // -> bool, default true when absent
  bag.Set(kShowGrid, false);
  ```
  `SettingsKey<T>` binds name + type + default; `Get/Set` are type-checked against it.

### 3.3 Layering
`Settings` is an ordered stack of layers: **Default (in-memory) < User (global file) < Project (file)**.
- **Read** (`Section<T>()`): the top-most layer that has the section wins — **whole-section override**,
  not field-merge. Predictable, and a field missing from a higher layer's XML simply keeps the struct's
  member-initializer default (so adding fields later is backward-safe). No recursive join to reason about.
- **Write**: targets a chosen layer (default **User**); `Section<T>(Layer::Project)` writes/creates the
  section in that layer.
- Default layer is never persisted — it is the structs' own defaults.

### 3.4 Unknown-section passthrough (self-describing backends only)
The store holds each section as either a **live typed object** (once claimed via `Section<T>()`) or a
**captured generic node** — an unknown-to-this-build section read into the dynamic `Value`/`PropertyBag`
tree (§3.2). On save, live objects are re-serialized and captured nodes are re-emitted from the generic
tree — so a newer editor's extra settings are not lost when an older editor opens and re-saves.

This works only on a **self-describing** backend (XML: the reader can enumerate an unknown section's
children). A **positional binary** backend can't capture an unknown-shape section (no keys/lengths in
the stream) — the same limitation as binary scene records (see the roadmap "length-prefix records"
item). Settings files use XML, so passthrough holds there; on binary, unknown sections are dropped.
Phased later (§7) since it needs the serializer to expose generic read of an unknown object.

### 3.5 Change notification
`settings.OnChanged(Function<void(StringView sectionName)>)` — fired on `MarkChanged`/`Set`. Subsystems
subscribe to react (theme section changed → re-theme). Lightweight; no per-field granularity in v1.

### 3.6 Storage
- **User/global** settings → the **user data dir**: `$XDG_DATA_HOME/draconic` (Linux, fallback
  `~/.local/share/draconic`), `%LOCALAPPDATA%\Draconic` (Windows), `~/Library/Application Support/Draconic`
  (macOS) — file `editor.settings.xml`.
- **Project** settings → the project dir (e.g. `project.settings.xml`).
- Resolved via a new **`core` user-data-dir helper**, also reused by the export templates root (§5).
- **Backend-agnostic**: the store serializes through a caller-supplied **`SerializerFactory`** (the
  `ISerializer` abstraction) + `BeginVersionedPayload` per section — it is **not** tied to XML. The
  editor passes `XmlSerializerFactory()` for hand-editable files; a headless test can pass the binary
  factory. (Human-readable XML is a caller choice, not a coupling.)

File shape:
```xml
<settings>
  <section type="draconic::editor::EditorExportSettings" version="1">
    <templatesRoot>/home/me/draconic/templates</templatesRoot>
  </section>
  <section type="..." version="..."> ... </section>
</settings>
```

---

## 4. Where it lives

- **`draconic.core`** — the primitive: `Settings` (layers, load/save, notification), `PropertyBag` +
  `Value` + `SettingsKey<T>`, and the **user-data-dir** helper. Runtime-usable (no editor dep).
- **Editor** (`draconic.editor.core`) — the concrete sections (`EditorExportSettings`, later
  `EditorLayoutSettings`, `RecentProjects`, …) + wiring the User file in the user data dir into the
  `EditorContext`. Export reads `EditorExportSettings.templatesRoot` for the templates root override.

---

## 5. API sketch

```cpp
enum class SettingsLayer { Default, User, Project };

class Settings {
public:
    // Typed section: resolved top-most for read; pass a layer to target a write copy.
    template <typename T> T& Section(SettingsLayer write = SettingsLayer::User);
    template <typename T> const T* Find() const;          // null if no layer has it
    template <typename T> void MarkChanged();             // notify + flag the owning layer dirty

    // Backend-agnostic: caller supplies the SerializerFactory (XML in the editor, binary in tests).
    Status LoadLayer(SettingsLayer, vfs::IFileSystem& root, StringView file, SerializerFactory);
    Status SaveLayer(SettingsLayer, vfs::IWritableFileSystem&, StringView file, SerializerFactory) const;

    void OnChanged(core::Function<void(core::StringView)> cb);
};

// Dynamic bag section
struct PropertyBag : ISerializable { /* Map<String,Value>; Get/Set via SettingsKey<T> */ };
```

---

## 6. Decisions (recommended)

- **Typed sections primary; dynamic bag secondary.** Reflection makes typed the ergonomic default;
  the bag covers the genuinely-dynamic tail.
- **Whole-section override layering** (not field-merge) — predictable; struct defaults cover missing
  fields. Field-merge only if a real need appears.
- **Unknown-section passthrough** on save — don't clobber a newer build's settings.
- **User-data-dir helper in `core`**, shared with the export templates root.
- **Owned service** (on `EditorContext`), injected — no global singleton.
- **Backend-agnostic** — the store serializes through the `ISerializer`/`SerializerFactory` abstraction;
  the caller picks the backend. Not tied to XML. The editor chooses XML for hand-editable files.

---

## 7. Phased plan

1. **Core primitive** — `Settings` (typed sections + Default/User layers + factory-driven load/save +
   `OnChanged`) + the user-data-dir helper. Tests: section round-trip (XML *and* binary factory), layer
   override, defaults.
2. **Editor wiring** — `EditorExportSettings` + load/save the User layer from the user data dir into
   `EditorContext`; expose the templates root to export (satisfies export §5 decision 2).
3. **Dynamic bag** — `PropertyBag` + `Value` + `SettingsKey<T>` (when a consumer needs open-ended keys;
   the export path does not, so this can trail).
4. **Unknown-section passthrough** — capture unknown sections into the generic tree + re-emit; needs a
   self-describing-backend read capability on `ISerializer` (§3.4).
5. **Project layer** + more editor sections (layout, recent projects, theme) as the editor grows.
6. **Settings UI** — an editor preferences panel (edits sections; uses the shell file dialogs for path
   pickers). Ties back to export Phase 4.
