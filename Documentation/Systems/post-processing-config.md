# Post-Processing Configuration

> Status: CURRENT
> Verified: 2026-08-12 @ b07dab67
> Track: [[post-processing-config]]

The post stack (tonemap/AgX, bloom, AO, SSR, TAA, FXAA) is authored data, not hardcoded knobs: edited
per scene, resolved and applied PER VIEW, serialized with the scene, shippable, with editor viewport
overrides. Phases 1 + 2a + 2b + 3 shipped; phases 4-5 (breadth) pending.

## Authoring surface (per scene)

A dedicated `PostProcessSystem` + reflected `PostProcessSettings` on the render subsystem
(`RenderComponents`), added to every scene beside `EnvironmentSystem`, auto-surfacing as a
"Post Processing" scene-settings inspector section (undoable) that serializes with the scene. Exposure is
authored in EV/stops (tonemap applies `2^EV`; 0 = neutral). Fields: `exposureEV`, `tonemapOperator`
(Clamp / AgX), bloom (enable + threshold/knee/intensity), AO (`AoMode` + strength/radius/intensity), SSR
(enable + intensity), and a single `AaMode` (Off / FXAA / TAA) + TAA/FXAA sub-params. Defaults MATCH the
old hardcoded RenderSubsystem values, so authoring changed no visuals until edited.

## Resolved per view

Per-view because the engine renders multiple views per frame (split-screen) and per-view transient post
state (TAA history/jitter/sub-rect) already lives on `ViewSettings`. `ViewPostConfig` (primitives, in
the snapshot `:data` layer) sits on `ViewSettings`; `RenderSubsystem::RenderScene` resolves each view's
post from the scene's `PostProcessSystem` via `ResolveScenePost` (exposure EV -> linear `2^EV`; `AoMode`
-> u32; `aaMode` -> exclusive `taaEnabled`/`fxaaEnabled`; `tonemapOperator` -> the AgX bool), or a
latched global override (the programmatic setters, for samples' ImGui panels). The compose reads
`v->Settings().post`. TAA-per-view is made tractable by pre-resolving `needsMotion` onto `ViewPostConfig`
(the forward pass reads it at execute), so jitter, motion vectors, the TAA resolve, and FXAA are all
per-view; the Halton phase advances once/frame if ANY view used TAA. SSR overrides the frame-global
`SsrPass::Params` intensity per view. The tonemap operator is a push float with a Clamp branch (saturate
+ sRGB OETF) in the shader; AgX stays default.

## Editor viewport overrides

`ViewPostOverride` (`disablePost` master + per-effect `disableBloom`/`Ao`/`Ssr`/`Aa`) is an optional
`RenderScene` param (mirrors `cameraOverride`), applied via `ApplyViewPostOverride` to the resolved
per-view post before the `needsMotion` finalize - ephemeral, never serialized. `ScenePage` has a "Post"
viewport-toolbar button opening a checkable show-flags menu bound to a page-local override.

## Deferred

- **Phase 4 - breadth**: color-grading LUT, depth of field, vignette, motion blur, auto-exposure. None
  built (no LUT/DoF/vignette/motion-blur/auto-exposure in the render code).
- **Phase 5 - localized looks**: per-camera overrides + post-process volumes (blend by region). Not
  built.
