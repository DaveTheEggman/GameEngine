# GameInstance - deferred follow-ups

> Status: CURRENT
> Track: [[game-instance-track]]

The track shipped end to end (`Documentation/Systems/game-instance.md`). Two items remain, neither
blocking.

- **Player multi-scene compositing.** `DefaultApplication::OnRenderWindow` renders the primary group's
  scenes; N instances in the PLAYER would hit the single-scene render path (multiple active scenes would
  each clear; compositing is a later concern). The EDITOR is unaffected - each instance renders into its
  own viewport RT. Only relevant if the standalone player ever needs multiple simultaneous instances
  on-screen.

- **One shared editor scene manager (alternative to per-page).** Each editor page owns its own
  `SceneManager` today (cleanest lifetime + isolation). The alternative - one editor-owned manager that
  pages create/destroy scenes in - gives a simpler mental model and a cross-page sweep scoped to exactly
  the editing group, at the cost of one long-lived shared owner + slightly more coupling. Switching is a
  bounded change (a shared manager on the editor + pages create in it + drop the page-owned members);
  do it only if the registry-wide `ForEachScene` sweep breadth ever becomes a real problem.
