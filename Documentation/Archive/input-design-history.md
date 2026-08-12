# Input - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/input.md
> Track: [[input-subsystem]]

NON-AUTHORITATIVE. The reference survey and design rationale behind the 2026-07 action-mapping build.
Present-tense truth is `Systems/input.md`; the full original design doc (goals, phasing P1-P3, the
detailed per-section design) is in git at the P0 commit 3b92560d. Kept for the "why".

## Reference survey - conclusions

**Sedulous** (`Sedulous.Engine.Input`): a complete context/action/binding hierarchy that is DEAD CODE
- zero consumers; every sample raw-polls the shell. Lessons: (a) build the action layer only WITH a
real consumer proving it (samples + play-in-editor from day one); (b) its `InputValue {X,Y}` with
`AsBool => X>0.5` is lossy - don't collapse types; (c) no data-driven maps, no rebind capture, no
persistence = the gaps that made it unused; (d) it ran THREE parallel input stacks - we avoid that
(draconic.ui consumes the same shell events via the bridge).

**ezEngine** (`Core/Input`) - the strongest base model: string-keyed input slots (every physical
input a named float), actions in first-class input sets evaluated simultaneously, `SetExclusiveInputSet`
for menus; per-slot dead zones, per-binding response-curve scale, and the standout per-action
time-scaling with mouse-delta slots flagged `NeverTimeScale` (a rate vs an absolute is a property of
the binding, not gameplay code). `GetPressedInputSlot(mustHave, mustNot)` is a ready-made rebind-capture
primitive. Awkward: 3-alternative cap, esoteric input-area filters, global static state.

**Godot** (`core/input`) - the cleanest value model: every action query yields strength/raw_strength
in [0,1]; axes synthesized at the query layer (`get_axis`, `get_vector` with circular dead zone +
normalization); per-device state folded OR(pressed)/MAX(strength); frame-counter `just_pressed`
correctness; physical-keycode matching. Awkward: no action sets at all, per-action-only dead zone,
DIY rebind persistence.

**Flax** (`Source/Engine/Input`) - designer-friendly axis smoothing (Sensitivity ramp, Gravity
recenter, Snap zero-on-flip, Scale invert); bindings in a reflected `InputSettings` asset. Awkward:
actions and axes are two parallel systems, no contexts, no consumption, focus-only gating.

## Key design calls

- The engine ACTION layer is engine-global, not per-scene (input is per PLAYER, not per world); it
  sits on the existing raw device layer (`foundation.shell`), not a reinvented one.
- Declared action kinds (Button / Axis1D / Axis2D), not Sedulous's collapsed `{X,Y}` - a
  binding-to-kind mismatch is an editor error, not a silent 0.
- Set stack with priority resolution + exclusive push (ez), plus held-then-suppressed latching so a
  held "Fire" does not re-trigger when a menu closes.
- UI-vs-game arbitration wired by construction (a consumption mask the game-UI sets each frame, UI
  first in the tick) rather than Sedulous's never-wired `UIConsumedInput` flag.
- User rebinds are a settings overlay over the pristine asset (avoids Godot's DIY persistence).
- Play-in-editor forwarding through an `IInputSourceProvider` seam (raw shell for the game host, the
  viewport `InputSurface` for the editor) - fixes Sedulous's unforwarded editor-viewport input.

## Open questions (as resolved)

1. Namespaced action queries (`"Gameplay/Fire"`) vs flat with set resolution -> flat (ez-style); sets
   are a gating mechanism, not an identity namespace; duplicate names resolve by priority.
2. Gamepad rumble in this subsystem or on the raw `IGamepad` facade -> raw facade for now (see
   backlog).
3. Per-scene input for split-screen -> covered by the future `PlayerInput` (per-player pairing)
   without making the core per-scene.
