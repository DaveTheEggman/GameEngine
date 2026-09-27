# Scene format reference, generated from the code

> STATUS: PROPOSED 2026-09-26, not scheduled. Sized M (P0-P2), L with P3. Origin: authoring
> Sedulous's PaperKid over the MCP. `scene_write` accepts a scene's XML, but nothing the MCP
> serves says what goes inside it, so the agent wrote a throwaway probe that built a scene in
> code, saved it, and read the XML back before it could author a single component. Every
> citation below was checked against `editor-mcp` at 7c835966. Read CONVENTIONS.md first.

## Goal

An agent (or a person) authoring a scene, prefab or settings block by hand has an exact,
current reference for the text format: a complete worked example scene and a schema of every
component and settings block. Both are produced FROM THE CODE, so they cannot drift from what
the reader accepts:
- a build writes them beside its executables, so a given build carries its own exact format;
- the MCP hosts serve them;
- a checked-in copy plus a golden test turns every wire change into a readable diff in review.

Not goals:
- Changing the scene wire format. Enums stay numeric on the wire; the schema names them.
- A structured authoring API (`entity_create`, `component_add`, ...). That is a separate
  proposal and would consume this schema, not replace it.
- Hand-written format prose. `Scenes.md` keeps the concepts and points at the generated files.

## The problem: reflection is not the wire format

`type_info` looks like the obvious source and is not one. The scene wire format is written by
hand-coded `Serialize` functions; reflection is a separate, parallel description. The two differ
in field set, field order and encoding, and reflection carries no defaults.

- **The component contract** (`Code/Foundation/Scene/Component.cppm:244-309`,
  `SerializableComponentManager<T>`):
  - The wire name is a constructor argument (:248).
  - The payload is an ADL `Serialize(ISerializer&, T&)` wrapped in
    `BeginVersionedPayload(ar, TypeOf<T>())` (:256-277).
- **Encoding differs.** `RigidBodyComponent`'s `Serialize`
  (`Code/Engine/Engine.Physics/PhysicsComponents.cppm:93-119`) writes `motion`, `layer` and
  `shape` as raw `u8` casts. Its reflection (`Code/Engine/Engine.Physics/PhysicsSubsystemImpl.cpp:227-260`)
  declares them as enum-typed properties.
- **Order differs.** The same reflection lists `isTrigger, continuousCollision, mass,
  collisionGroup, collisionShape, material`, while the wire order is `isTrigger,
  collisionGroup, collisionShape, material, heightfield, continuousCollision, mass`. Order
  matters: the XML reader searches FORWARD from a cursor by key
  (`Code/Foundation/Xml.Serialization/XmlSerializer.cppm:325-369`), so fields must follow the
  `Serialize` order, and a missing field fails the read.
- **The field set differs.** `PhysicsSceneSettings` serializes `groupNames` and
  `groupCollides` (`PhysicsComponents.cppm:300-313`), but its reflection omits them
  (`PhysicsSubsystemImpl.cpp:335-341`). `ScriptComponent` reflects no properties at all
  (`Code/Engine/Engine.Script/ScriptSubsystemImpl.cpp:49-54`), while its payload carries
  behaviours and overrides (`Code/Engine/Engine.Script/ScriptComponents.cppm:30-130`).
- **No defaults.** `PropertyInfo` (`Code/Foundation/Core/Reflection/Reflection.cppm:243-259`)
  has name, type, flags, accessors, address and attributes, and no default value. The only
  source of a default is a default-constructed `T{}`.
- **The data version chain is unguessable, and unforgiving.** Each record carries
  `dataVersions`: `{u64 type, u32 version}` for the concrete type and every versioned base
  (`Code/Foundation/Core/Serialization/Serialize.cppm:266-300`). The type id is FNV-1a over
  `namespace + "::" + name` (`Code/Foundation/Core/RTTI/TypeInfo.cppm:70-76`). Any mismatch in
  count, id or version fails the payload (`Serialize.cppm:301-316`), and with it the WHOLE
  `LoadScene` (`Code/Foundation/Scene.Resource/SceneResourceImpl.cpp:2046-2050`).
  `type_info` does not expose `dataVersion`.
- **The registry cannot enumerate what is needed.**
  - Enums and most scene components are not in the registry: `EnumBuilder::Build` only
    patches `TypeOf<E>` (`EnumReflection.cppm:62-73`), and physics component registration
    never calls `Register` (`PhysicsSubsystemImpl.cpp:411-427`).
  - There is no per-domain listing (`Code/Foundation/Core/RTTI/TypeRegistry.cppm:187-316`).
  - What does enumerate the components is a composed scene: `engine::AddAllSceneManagers`
    (`Code/Engine/Engine.SceneSurface/SceneSurfaceImpl.cpp:92-100`) then
    `Scene::ForEachManager` / `ForEachSystem` (`Code/Foundation/Scene/Scene.cppm:327,340`).
- **References are opaque.** `EntityRef` (`Code/Foundation/Scene/Entity.cppm:58-78`) and
  `resource::Ref<T>` (`Code/Foundation/Resource/ResourceModule.cppm:126-208`) both serialize as
  a bare guid string. Neither is `REFLECT_VALUE`'d, so `type_info` shows `"<value>"`.
  - Code recognises `EntityRef` by pointer identity (`SceneResourceImpl.cpp:856-880`).
  - The inspector hard-codes `Ref<T>` per asset type (`Code/Editor/Editor.Scene/InspectorViewImpl.cpp:708-718`).
  - Nothing generic says "this field is a resource reference to asset type X".
- **The script override hash is a second unguessable.** Overrides are keyed by
  `ScriptPropertyNameHash(name)`, FNV-1a 64 over the UTF-8 name
  (`Code/Foundation/Script.Resource/ScriptResource.cppm:52-55`, `Code/Foundation/Core/Hash.cppm:22-33`).
  A `kind` code (`ScriptPropertyType`, `ScriptResource.cppm:38-49`) selects which single
  payload key follows (`ScriptResource.cppm:123-154`).

## What exists (checked, cited per the Specs rule)

- **Scene stream writer and reader** (`Code/Foundation/Scene.Resource/SceneResource.cppm`):
  - `SaveScene` :1353-1377 always writes XML text; `LoadScene` :1347;
    `TranscodeSceneStreamToBinary` :1344.
  - Header magic `0xD5C35CEE` and version 3 at :57-58.
  - Section order: header, name, entities (flat records), components, systemSettings,
    prefabMode, prefabInstances (`SceneResourceImpl.cpp:76-569`).
- **The settings seam**: `SceneSystem::SettingsType / SettingsInstance / SettingsId /
  SerializeSettings` (`Code/Foundation/Scene/SceneSystem.cppm:61-74`).
  - Five blocks exist: `physics`, `environment`, `postprocess`, `sceneScript`, `navigation`
    (declared at `Engine.Physics/PhysicsSubsystem.cppm:99`, `Engine.Render/RenderComponents.cppm:541,661`,
    `Engine.Script/ScriptSubsystem.cppm:1317`, `Engine.Navigation/NavigationSubsystem.cppm:45`).
  - Text records are `{system, dataVersions, settings}` (`SceneResourceImpl.cpp:415-426`).
- **The serializer interface is a plain virtual surface**
  (`Code/Foundation/Core/Serialization/ISerializer.cppm:62-128`): `Mode`, the version scope,
  `Key`, `BeginObject/EndObject`, `BeginArray/EndArray`, `Scalar(void*, ScalarKind)`, `Text`,
  `Blob`, `GuidValue`, `FailPayload`. A new backend is a subclass; nothing in the scene code
  assumes a concrete one.
- **Reflection metadata worth joining in**:
  - Enum value names: `EnumValueName` / `Enumerators` (`EnumReflection.cppm:86-127`).
  - Attributes: `displayName`, `category`, `description`, `range`, `visibleWhen`
    (`Reflection.cppm:512-556`, `TypeBuilder::PropAttribute` :1752).
  - `dataVersion` / `minReadDataVersion` on `TypeInfo` (`TypeInfo.cppm:62,66`).
- **The docs:// channel**:
  - `RegisterShippingDocResources` (`Code/Editor/Editor.Mcp/ProjectTools.cppm:472-510`) serves
    every `*.md` in the located directory, re-read on each request.
  - `LocateShippingDocs` (:553-589) walks up from the executable, then the working directory.
- **Build-time precedent**: samples run an engine-built tool at build time by path
  (`Code/Samples/WebScene/CMakeLists.txt:52-58`, `BUILDSYSTEM_HOST_BIN_DIR` in the root
  `CMakeLists.txt:166,189`).
- **Golden precedent**: none. Tests that read the source tree get `RAPTOR_SOURCE_DIR`
  (`Code/Editor/Editor.Core.Tests/CMakeLists.txt:23`), which is the hook a golden test needs.
- **Reflection registration must precede writing.** Until `RegisterAllSceneComponentReflection()`
  runs, `TypeOf<T>().id` is the compiler-signature id, not the disk id
  (`TypeInfo.cppm:96-111`). `Code/Tools/Tools.Mcp/Main.cpp:80` calls it for exactly that reason.

## Decisions

### D1. The `Serialize` bodies are the source of truth, captured by a recording serializer

A new `ISerializer` backend in WRITE mode, `SchemaRecorder`, is driven through the real write
path over default-constructed instances. It records a tree of what the body did:
- each `Key` with the `ScalarKind` and default value that followed it;
- the nesting of `BeginObject` / `BeginArray`;
- `Text` and `GuidValue` fields.

A component is recorded by adding it with `AddDefaultComponent`
(`Component.cppm:38-73`) to an entity in a composed scratch scene and calling the manager's
`WriteComponent`. A settings block is recorded through `SerializeSettings`.

Because this is the code that writes real scenes, the recorded order, keys, kinds and defaults
ARE the wire format. Reflection never decides structure; it only annotates (D3). The recorder
also captures the `dataVersions` chain as written, so the schema's type ids and versions are
the ones the reader will check.

### D2. Two generated files

**`SceneExample.scene.xml`** is a real `SaveScene` of a scene built in code:
- every registered component on some entity, at its defaults;
- every settings block;
- a parent and child pair;
- a script behaviour carrying one override of each `ScriptPropertyType` kind;
- one prefab instance, if the composition allows it.

It is exactly what `scene_write` accepts, because it is what the engine writes.

**`SceneSchema.json`** is the recorder's trees plus annotations:
- a header section: magic, stream version, section order, prefab mode values;
- per component and per settings block:
  - `wireName`, with the `system` id for settings;
  - `dataVersions`: the chain with ids in decimal and hex, plus the type names they hash from;
  - `fields` in wire order, each with:
    - `key`;
    - `kind` (`u8` / `f32` / `string` / `guid` / `object` / `array` ...);
    - `default`;
    - nested `fields` for objects and array elements;
    - the annotations from D3.
- a `scriptOverrides` section: the hash algorithm with a worked example (a named field's hash
  in decimal), the kind table, and which payload key each kind uses.

JSON is the MCP's interchange format and never engine data, so a JSON schema file is within the
no-JSON-for-engine-data policy (`mcp-agent-access.md`).

### D3. Reflection annotates by joining on the key

Each recorded field is joined to the component's reflected property of the same name when one
exists. It then gains:
- `enum`: the value names, when the reflected type is an enum and the wire kind is an
  integer. This is what makes `motion = 2` readable.
- `displayName`, `description`, `range`, `category`, `visibleWhen`.
- `ref`: `entity` when the reflected type is `TypeOf<EntityRef>()`, including array elements
  through the container element type.

A field with no reflected twin is emitted unannotated and counted in a `unreflected` list per
type. That list is a useful finding in its own right; `groupNames` would show up there today.

### D4. Resource reference targets need one new piece of registration [DISCUSS]

Nothing generic records "this `Ref<T>` field points at asset type X". The recommendation is for
`TypeBuilder::Property` to attach a `resourceType` attribute automatically when the member type
is `resource::Ref<T>`, naming `T`'s reflected type.
- It is detected in the template at registration time, which is capability flowing INTO
  reflection at registration (CONVENTIONS, Code style).
- It needs no per-component edits, and the inspector's hard-coded table could later read the
  same attribute.

Until then, a guid field that does not join to an `EntityRef` property is emitted as
`ref: "guid"`, unspecified.

### D5. Determinism is part of the contract

The generator must produce byte-identical output for the same code:
- entity and asset guids from a seeded `Random` (`Guid::Generate(rng)`);
- components and settings in composition order (`kAllModules`, `SceneSurfaceImpl.cpp:41-80`),
  then plugin contributions in registration order;
- the existing XML float formatting;
- no timestamps, paths or build stamps in either file.

### D6. One library entry point, three consumers

`GenerateSceneReference(outExampleXml, outSchemaJson)` lives beside the composition it
enumerates, in Engine.SceneSurface or a small `Engine.SceneReference` module if the recorder
should not ride on SceneSurface. It calls `RegisterAllSceneComponentReflection()` first. Three
consumers share it:

1. **Build step.** A tool mode (recommended: `Tools.Export --emit-scene-reference <dir>`)
   runs as a post-build custom command in the `BUILDSYSTEM_HOST_BIN_DIR` pattern. It writes
   both files to `Bin/<config>/<platform>/SceneReference/`, so every build carries its own
   format.
   - Tools.Export already links `Engine::SceneSurface` for its transcode scratch scenes
     (`Code/Tools/Tools.Export/CMakeLists.txt:12`, `Main.cpp:51,121`).
   - Tools.Cook does not, since it links `Pipeline::Registration` and `Engine::ScriptSurface`
     only (`Code/Tools/Tools.Cook/CMakeLists.txt:3-8`).
2. **The MCP hosts.** The files are served as `docs://generated/SceneExample.scene.xml` and
   `docs://generated/SceneSchema.json`, with MIME types `application/xml` and
   `application/json`.
   - The host generates them in memory from its own registrations at startup, rather than
     reading the staged files. That keeps them true to the running binary even when no build
     step ran.
   - `RegisterShippingDocResources` gains the generated pair alongside its `*.md` listing.
3. **Golden test.** `Documentation/Shipping/Generated/` holds a checked-in copy of both files.
   A test in the Engine.SceneSurface (or Integration.Mcp) suite regenerates them and compares
   byte for byte, reading the checked-in copy through `RAPTOR_SOURCE_DIR`.
   - On mismatch it writes the fresh files to `.test-scratch/` and fails with the one command
     that refreshes the checked-in copy.
   - A wire change therefore cannot land without its reference diff in the same commit.

### D7. Hand-written docs keep what does not drift

`Scenes.md` explains concepts only: what entities, components, settings blocks, prefab
instances and script overrides ARE, and the validate-first workflow. For anything exact it
points at the two generated resources, and it carries no inline XML. While here, it takes
three corrections found during this research:
- The settings blocks are `physics, environment, postprocess, navigation, sceneScript`, not
  "physics, post-processing, audio" (`Documentation/Shipping/Scenes.md:26-29`).
- A stale data version fails the WHOLE scene load, not one record with a warning.
- `McpGuide.md` names the generated resources in its "first moves" list.

## Found along the way (fix in P2, cited)

- `scene_validate`'s tool description says its check is "STRUCTURAL"
  (`Code/Editor/Editor.Mcp/SceneTools.cppm:240`). Its file header (:11) and `Scenes.md` say
  payloads are validated in full. The description should match what the tool does.
- `LocateShippingDocs` accepts a `KnownIssues.md` staged beside the executable, but no build
  or dist script stages the docs directory (`scripts/build-editor-dist.sh:70-95` copies
  binaries and `Data/Assets` only). A distributed host therefore serves no `docs://` at all.
  The generated reference makes that gap sharper; P2 stages `Documentation/Shipping` with it.

## Phases

- **P0: the recorder and the schema** (M).
  - `SchemaRecorder` (an `ISerializer` backend) and the schema model.
  - `GenerateSceneSchema` over a composed scratch scene, covering every component manager and
    settings system, plus the joins of D3.
  - Tests:
    - RigidBody's recorded order and kinds match its `Serialize` body (`motion` is `u8` with
      enum names; `mass` comes last);
    - `physics` settings list `groupNames` and report it unreflected;
    - an `EntityRef` field is annotated `ref: entity`;
    - the `dataVersions` chain recorded for RigidBody is `{FNV("rtti::engine::physics::RigidBodyComponent"), 3}`;
    - the script override section's worked hash equals `ScriptPropertyNameHash` of its name.
- **P1: the example and the golden** (S).
  - `GenerateSceneExample` (D2 content, D5 determinism).
  - The checked-in `Documentation/Shipping/Generated/` pair and the golden test.
  - Tests:
    - the example loads through `LoadScene` into a fully composed scene with an ok status;
    - `scene_validate` on it is valid with no warnings;
    - two generations are byte identical;
    - the golden matches.
- **P2: delivery** (S).
  - The `Tools.Export --emit-scene-reference` mode and its post-build command.
  - docs:// serving of the generated pair in both hosts.
  - Staging `Documentation/Shipping` beside the executables in the dist scripts.
  - The `Scenes.md` rewrite and the two fixes above.
  - Tests:
    - `resources/list` includes both generated resources on the stdio host;
    - reading them returns the same bytes as `GenerateSceneReference`.
- **P3 (optional): `component_schema` tool.** One type's schema entry by wire name or type
  name, answered from the same model. It is for an agent that wants one component without
  reading the whole schema. Annotated read-only.

## Acceptance

- An agent with only the MCP can write a valid scene containing any component or settings
  block, using `docs://generated/*` and no probe.
- A wire change (a new field, a version bump, a reorder) fails the golden test until the
  checked-in reference is regenerated in the same commit.
- Both files are byte identical across two runs and both compilers.
- Green on clang and gcc; ASAN clean for the new tests.

## For the Beef port (Sedulous)

The OUTPUTS are the contract: the two file names, the JSON shape of `SceneSchema.json`, the
docs:// URIs, the golden test's rule, and the build step's placement. Sedulous produces the
same files by its own means:
- Its serializable metadata is comptime rather than runtime reflection, so the join of D3 and
  the resource target of D4 may come straight from comptime data.
- Its components with hand-written `Serialize` bodies still need the recording backend of D1,
  since the rule "the write path is the truth" holds in both engines.
