# Draconic Input — Action Mapping (design)

Status: APPROVED 2026-07-17; P1 SHIPPED 8bf26b1; P2 SHIPPED c8bf24e (interactions,
rebind overlay + capture, InputMapPage w/ Listen, Wren `Input` facade - statics on a
foreign class since the Wren backend cannot inject host globals) + the play-in-editor
device slice of P3 (Game-tab viewport provider). TOUCH SHIPPED fd93a68 (Steam Deck
consumer: TouchButton regions + floating TouchStick, sample-visualized); engine TIME SCALE
live (0efe632); rebind overlay consumer = the InputActions sample. REMAINING: consumption
mask awaits the game-ui subsystem; PlayerInput pairing awaits a multiplayer consumer.
Scope: the engine-level ACTION layer. The raw device layer already exists and ships
(`draconic.shell`: tagged `InputEvent` stream, `IKeyboard`/`IMouse`/`IGamepad`/`ITouch`
facades, `InputSurface`/`InputRouter` for transformed+gated viewport input).

## 1. Goals

- Gameplay reads **named actions** ("Jump", "Move", "Fire"), never raw keys — bindings are
  data, swappable per project and per user.
- Bindings are an **asset**: authored in the editor, cooked like everything else, loaded by
  the player, rebindable + persistable at runtime.
- First-class **action sets** (contexts): "Gameplay" vs "Menu" vs "Vehicle", with an explicit
  priority/exclusivity story — the #1 gap in every surveyed engine except ez.
- One value model spanning digital + analog + 2D, per-device aggregation, frame-accurate
  edges, working UI-vs-game arbitration.
- Scripting parity: Wren sees the same action API as C++.

## 2. Reference survey (what to keep / what to avoid)

**Sedulous** (`Sedulous.Engine.Input`): a complete context/action/binding hierarchy that is
**dead code** — zero consumers; every sample raw-polls the shell. Lessons: (a) build the
action layer only WITH a real consumer proving it (our samples + play-in-editor from day
one); (b) its `InputValue {X,Y}` with `AsBool => X>0.5` is lossy — don't collapse types that
way; (c) no data-driven maps, no rebind capture, no persistence = the actual gaps that made
it unused; (d) it also had *three* parallel input stacks (Engine.Input, UI's own enums, raw
shell) — we already avoid this: draconic.ui consumes the same shell events via the bridge.

**ezEngine** (`Core/Input`): the strongest base model. String-keyed "input slots" (every
physical input is a named float), actions grouped in first-class **input sets**, evaluated
simultaneously, with `SetExclusiveInputSet` for menus. Per-slot dead zones, per-binding
response-curve scale, and the standout insight: per-action **time-scaling** with mouse-delta
slots flagged `NeverTimeScale` (a rate vs an absolute is a property of the binding, not
gameplay code). `GetPressedInputSlot(mustHave, mustNot)` is a ready-made rebind-capture
primitive. Config = DDL file + editor dialog. Awkward: 3-alternative cap, esoteric
"input area" filters, global static state.

**Godot** (`core/input`): the cleanest value model. Every action query yields
`strength`/`raw_strength` in [0,1]; axes are **synthesized at the query layer**
(`get_axis(neg,pos)`, `get_vector(4 actions)` with **circular** dead zone + normalization).
Explicit per-device `DeviceState` folded OR(pressed)/MAX(strength). Frame-counter
`just_pressed` correctness (input mid-physics-tick schedules `frame+1`). Physical-keycode
matching for layout independence. Awkward: **no action sets at all** (global map, gating
pushed onto the scene tree), per-action-only dead zone, DIY rebind persistence.

**Flax** (`Source/Engine/Input`): designer-friendly axis smoothing (`Sensitivity` ramp,
`Gravity` recenter, `Snap` zero-on-flip, `Scale` invert) — Unity-familiar; bindings live in a
reflected JSON `InputSettings` asset edited by the generic property grid. Awkward: actions
and axes are two parallel systems, no contexts, no consumption, focus-only gating.

## 3. Architecture

New library: **`draconic.input`** — depends on `draconic.core` + `draconic.shell` (+
`draconic.settings` for rebind persistence). No scene dependency: the action system is
engine-global, not per-scene (input is per *player*, not per world). A thin
**`draconic.input.subsystem`** hooks it into the runtime tick and scene scripting.

```
draconic.shell        events + device facades (EXISTS)
      │
draconic.input        InputMap (data) + ActionRuntime (evaluation) + rebind + persistence
      │
draconic.input.subsystem   runtime subsystem: tick, UI arbitration, script registration
```

### 3.1 Data model (the asset payload)

```
InputMap                                  // one asset = one whole game's bindings
 └─ ActionSet[]        name, priority     // "Gameplay", "Menu", "Vehicle"
     └─ Action[]       name, kind         // kind: Button | Axis1D | Axis2D (declared, not inferred)
         └─ Binding[]  (unbounded list)
             KeyBinding        physical scancode (layout-independent) + optional modifiers
             MouseButtonBinding
             MouseAxisBinding  dx/dy/wheel; NEVER time-scaled (ez rule)
             GamepadButtonBinding  device: index | Any
             GamepadAxisBinding    device, deadZone, invert, scale
             GamepadStickBinding   → Axis2D, circular deadZone
             Composite2DBinding    4 sub-bindings (WASD), normalize flag
             TouchBinding          (phase 3; region → Axis2D/Button)
         Processors (per action): smoothing {sensitivity, gravity, snap} for key-driven
             axes (Flax), response curve exponent (ez), time-scale flag (ez)
         Interactions (phase 2): Hold(seconds) | Tap(maxSeconds) | DoubleTap(window)
             — none of the surveyed engines have these; UE-style, small state machine
             per action, worth doing properly since Sedulous's lack was a cited gap
```

Declared action kinds (not Sedulous's collapsed `{X,Y}`): a Button queried as a vector is an
error surfaced in the editor, not a silent 0. Binding-to-kind mismatches validate at asset
save + cook.

### 3.2 Runtime evaluation (ActionRuntime)

Per frame, fed from the shell event stream + polled device facades:

- **Per-device state per action** (Godot): pressed OR-folds, strength MAX-folds across
  devices and bindings. `strength` ∈ [0,1] per component; `raw` bypasses processors.
- **Edges**: `WasPressed`/`WasReleased` computed against a frame counter; when we add fixed
  ticks (physics), mirror Godot's dual process/physics frame counters so `just_pressed` is
  exact in both.
- **Queries**: `IsDown/WasPressed/WasReleased/Value(f32)/Value2D(Float2)` on an `ActionRef`
  (name-hash resolved once, not string compare per query). Synthesized `Axis(neg,pos)` and
  `Vector2(negX,posX,negY,posY)` helpers with circular dead zone for ad-hoc composition.
- **Set stack**: all enabled sets evaluate every frame (cheap); the QUERY resolves through
  enabled sets by priority. `PushExclusiveSet("Menu")`/`Pop` makes every other set read
  released (with proper release-edges firing once, so a held "Fire" doesn't re-trigger when
  the menu closes — ez gets this right via `RequireKeyUp`-style latching; we adopt: an
  action suppressed while physically held stays suppressed until released).
- **Events**: optional `OnAction(setName, actionName, callback)` for UI-ish consumers;
  polling is the primary API (gameplay reads in Update).

### 3.3 UI-vs-game arbitration

Two mechanisms, both explicit:
1. **Consumption mask**: the game-UI subsystem (see game-ui.md) reports per-frame which
   input CLASSES it consumed (pointer, keyboard, text). The input subsystem masks matching
   binding sources for that frame. This replaces Sedulous's never-wired `UIConsumedInput`
   flag — it is wired by construction because both live in the runtime tick, UI first.
2. **Exclusive set** for modal states (pause menu pushes "Menu" exclusive). Gamepad UI
   navigation consumes via the same mask when a UI context has focus.

`WantsTextInput` (already exposed by draconic.ui) additionally suppresses ALL key bindings
while an EditText has focus — not just the keys UI consumed.

### 3.4 Play-in-editor

The editor viewport already provides gated, transformed device facades via
`InputSurface`/`InputRouter` (hover=mouse, focus=keyboard, click-to-focus). The
`ActionRuntime` takes its devices through an `IInputSourceProvider` seam; the game host
passes the raw shell, play-in-editor passes the viewport's `InputSurface`. This fixes, by
construction, Sedulous's "keyboard/gamepad not forwarded from the editor viewport at all".

## 4. Runtime resources

- **`InputMapResource`** (cooked product): the `InputMap` data model serialized versioned-
  binary, built from the source asset by a `DefaultAssetBuilder` (pure data, no processing —
  cook = validate + write-through). Runtime `resource::Ref<InputMapResource>`; the runtime
  subsystem binds the project's default map at startup (project settings gains
  `defaultInputMapId`, like `defaultScene`).
- **User rebind overlay**: NOT part of the asset. A `draconic.settings` section
  (`InputBindingOverrides`) stores per-action replacement binding lists in the USER settings
  file; the runtime applies overlay-over-asset at load and after each rebind. Reset-to-
  default = clear the overlay (the asset stays pristine — avoids Godot's DIY persistence).

## 5. Editor-side assets

- **`InputMapAsset`** (source, `draconic.input.editor`): the same data model + the standard
  Asset envelope. Created via the New Asset menu ("Input Map"); default template seeds
  "Gameplay" set with Move/Look/Jump/Fire examples.
- **Editing v1**: a dedicated `InputMapPage` (editor page, like the material page): tree of
  sets → actions → bindings with add/remove/reorder; binding rows use the reflection
  inspector's row machinery where possible. Everything undoable through the page's command
  stack.
- **Editing v2**: **capture dialog** — "Listen" button per binding row; implemented on the
  shell layer as `CaptureNextInput(filter)` (ez's `GetPressedInputSlot` shape: must-have /
  must-not-have capability flags so an axis rebind ignores key presses). Esc cancels.
- **Validation**: duplicate-binding warnings within a set (allowed across sets), kind
  mismatches, empty actions — surfaced inline in the page (same pattern as Save As inline
  errors).
- **Import**: none needed (created in-editor). Export/staging: cooked like any asset into
  the pak.

## 6. Scripting (Wren)

Registered via the existing reflection→script bridge:
`Input.action("jump").down` / `.pressed` / `.value` / `.vector`, plus
`Input.pushSet("Menu", exclusive: true)` / `Input.popSet()`. Action refs cached on the
script side by the binding layer (no per-call string hashing in the VM loop).

## 7. Portability notes

- SDL scancodes are already physical — layout-independent matching is the natural default;
  store scancodes, DISPLAY localized labels via SDL keycode translation.
- WASM: SDL/emscripten delivers the same events; gamepads arrive via the Gamepad API with
  user-gesture activation — the runtime must tolerate device counts changing at any frame
  (it already must, for hotplug).
- Touch bindings (phase 3) become the mobile/web story: virtual-stick regions declared in
  the map, rendered by game-ui.

## 8. Phasing

- **P1 — core runtime + asset**: data model, ActionRuntime (sets/queries/edges/aggregation/
  dead zones/processors), InputMapAsset + cooked resource + default-map project setting,
  hand-authored map; PROVE with a sample (WASD+stick camera in Sandbox) and the player.
  Unit tests: evaluation over synthetic event streams (no real devices needed).
- **P2 — editor page + rebinding**: InputMapPage, capture-next-input, user overlay
  persistence via settings, interactions (Hold/Tap/DoubleTap), Wren exposure.
- **P3 — UI arbitration + players + touch**: consumption mask wired to game-ui, exclusive-
  set latching polish, `PlayerInput` (per-player device pairing: a player owns a device set;
  actions resolve against the owner's devices — the model all three engines lack),
  TouchBinding + virtual sticks.

## 9. Open questions

1. Should action QUERIES be namespaced by set (`input.Get("Gameplay/Fire")`) or flat with
   set resolution (`input.Get("Fire")` searches enabled sets by priority)? Recommendation:
   flat resolution (ez-style) — sets are a gating mechanism, not an identity namespace;
   duplicate names across sets are allowed and resolved by priority.
2. Gamepad rumble as part of this subsystem (`action-triggered haptics`) or leave on the raw
   `IGamepad` facade? Recommendation: raw facade for now.
3. Do we ever need per-scene input (split-screen with per-viewport focus)? The PlayerInput
   design (P3) covers it without making the core per-scene.
