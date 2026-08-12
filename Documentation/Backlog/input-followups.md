# Input - deferred follow-ups

> Status: CURRENT
> Track: [[input-subsystem]]

The shipped stack (`Documentation/Systems/input.md`) covers P1-P2 + the touch and play-in-editor
slices of P3. What remains is the rest of P3, each scoped and consumer-gated.

- **Per-player device pairing (`PlayerInput`).** A player owns a device set; actions resolve against
  the owner's devices - the model all surveyed engines lack, and the clean way to do split-screen
  without making the core action system per-scene. DEFERRED: awaits a real multiplayer / split-screen
  consumer to design against (the same "build it with a consumer" lesson that Sedulous's dead input
  layer taught).

- **Action-triggered haptics.** Rumble stays on the raw `IGamepad` facade for now; promoting it to an
  action-triggered effect in this subsystem is a later call, once a game wants data-authored haptics.

- **Per-scene / split-screen input.** Follows from `PlayerInput` above; the core stays engine-global
  until a consumer needs per-viewport focus.
