# GameInstance - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/game-instance.md
> Track: [[game-instance-track]]

NON-AUTHORITATIVE. The retrofit diagnosis, the phased plan, and the ownership investigation behind the
2026-07 GameInstance track. Present-tense truth is `Systems/game-instance.md`; the full original design
doc (all sections, every file:line audit) is in git at the P0 commit 3b92560d. Kept for the "why".

## The finding: the run bracket existed twice

The player (`Tools/Player/main.cpp`) and the editor's Game tab (`GamePage`) hand-rolled the SAME
sequence: create scene -> load -> bind resources -> camera -> start -> simulate -> pair script<->scene
-> app launch hook -> start script -> ... -> stop (script exit -> unpair -> OnExit -> scene stop ->
destroy). The track did not invent a concept; it NAMED and extracted that duplicated bracket.
`GameInstance` = this bracket, owned.

## What blocked multiple instances (the original audit)

`DefaultApplication` was both the shared-services host AND the singular run state (`m_primaryScene`,
`m_scriptErrorHandler`, `m_scriptManager`/`m_scriptContext`/`m_game`). `ScriptSubsystem` owned a single
`m_runHost` shared by the game script AND every scene's behaviors (two runs of the same script would
share globals - the real work). `StartGameScript()` began with `StopGameScript()` (a second run killed
the first). `m_gamePage` was an editor singleton. `OnLaunch`/`OnExit` (app hooks) were called per Play.
Already fine: per-scene subsystem state, per-surface input, per-viewport rendering, and the
borrowed-vs-owned resource-manager split (the precedent that shared-vs-owned was already a deliberate
distinction).

## The two framings

The doc first described a retrofit (move singular state onto an owned `Array<GameInstance>`, keep
`m_primaryScene` as a per-instance current scene, generalize the time model with an instance term). Then
(2026-07-21, user: "no users, no backwards-compat - do it right") it committed to the clean target: two
explicit layers (engine host vs GameInstance), a `SceneManager` in the SCENE lib so the tick loop moves
without the scene layer ever learning about the runtime layer, and run ownership moved up to
`GameInstance`. Where they conflicted, the target won - and is what shipped (see the Systems doc).

## Run-host ownership investigation (step 3c)

The open question was "what run host does editor editing-scene Simulate use?" Investigation found
`ScenePage::StartSimulation` un-freezes the editing scene's `ScriptSceneSystem`, so editing-Simulate
runs script behaviors today (a real feature, not to regress). So run hosts are NOT exclusive to
GameInstances. Resolved model (shipped): run host and scene manager are paired; `SceneSubsystem` keeps a
host for its default/editing scenes, each `GameInstance` owns one for its group; at N=1 in the editor
these are DISTINCT (editing-Simulate on the subsystem's host, the Game tab on the instance's) - cleaner
than the old shared-host state. `SceneSubsystem` later became a PURE registry (no default manager;
commit 2231caf), every scene owner creating its own `SceneManager`.

## Precedent: Zero's `GameSession`

Zero achieves Ctrl+F5 multi-instance in-process on ONE shared engine: `GameSession` is the first-class
game-instance object, the editor holds a `GameArray` (a collection, not a singleton),
`PlayGame(SingleInstance | MultipleInstances)` either quits existing games first or appends. Zero does
NOT instantiate N engines - confirming the cut at the game-instance boundary, not the application/context
boundary.

## Editor scene-manager decision (per-page vs one-shared)

Shipped per-page (each `ScenePage`/`MaterialPage`/`GamePage` owns its `SceneManager`): cleanest
lifetime (closing a page destroys only its group), mirrors "each owner owns a manager", no shared-group
churn. Cost: "all editing scenes" sweeps go through the registry-wide `ForEachScene`, wider than one
editing group (harmless for prefab-rebuild/export-scan). The alternative (one editor-owned manager) is a
simpler mental model with an exactly-scoped sweep but one extra long-lived owner + more coupling.
Verdict: neither clearly better; switching later is a bounded change (kept as a backlog note).
