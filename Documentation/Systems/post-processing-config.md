# Post-Processing Configuration — authoring the look

Status: **Phase 1 SHIPPED** (2026-07-20, edde64c); phases 2-5 pending. Related:
[renderer.md](renderer.md), [renderer-improvements.md](renderer-improvements.md),
[editor.md](editor.md).

## Status (2026-07-20)

- **Phase 1 (authoring surface) SHIPPED** — dedicated `PostProcessSystem` + reflected
  `PostProcessSettings` on the render subsystem (Components.cppm), added to every scene beside
  `EnvironmentSystem`. Exposure is authored in **EV/stops** (tonemap will apply `2^EV`; 0 =
  today's neutral) — user decision 2026-07-20. Fields: exposureEV, tonemapOperator (Clamp/AgX),
  bloom (enable+threshold/knee/intensity), AO (AoMode+strength/radius/intensity), SSR
  (enable+intensity), a single `AaMode` enum (Off/FXAA/TAA) + TAA/FXAA sub-params. Defaults MATCH
  the current hardcoded RenderSubsystem values, so **no visual change yet**. Auto-surfaces as a
  "Post Processing" scene-settings inspector section (undoable), serializes with the scene, and
  round-trips (tested). Placement = **dedicated PostProcessSystem** (decided).
- **Phase 2 decision: PER-VIEW** (not frame-global) — the engine already renders multiple views
  per frame (split-screen) and per-view transient post state (TAA history/jitter/sub-rect) already
  lives on `ViewSettings`, so the authored config resolves onto `ViewSettings.post`.
- **Phase 2a SHIPPED (59e2682) — exposure/bloom/AO per-view.** `ViewPostConfig` (primitives, in
  the `:data` snapshot layer) on `ViewSettings`; `RenderSubsystem::RenderScene` resolves each
  view's post from the scene's `PostProcessSystem` (`ResolveScenePost`: exposure EV → linear
  `2^EV`; `AoMode` → u32) or a latched global override (the programmatic setters, for samples'
  ImGui panels); the Pipeline compose reads `v->Settings().post` for exposure/bloom/AO. Defaults
  match the old globals → no visual change until edited. **Needs on-screen verification** that
  editing the "Post Processing" inspector changes the viewport live.
- **Phase 2b SHIPPED (7e6194f) — AA (TAA/FXAA) + SSR + `tonemapOperator` per-view.** The whole
  authored block is now live. `ViewPostConfig` gained `agxTonemap`/`taa*`/`fxaa*`/`ssr*` and a
  resolved `needsMotion`; `ResolveScenePost` maps `aaMode` → the exclusive `taaEnabled`/
  `fxaaEnabled` and `tonemapOperator` → the `agx` bool. TAA-per-view was made tractable by
  pre-resolving `needsMotion` onto `ViewPostConfig` (the forward pass reads it via the bound view
  at execute), so jitter, motion vectors, the TAA resolve, and FXAA are all per-view; the Halton
  phase advances once/frame if any view used TAA. SSR overrides the frame-global `SsrPass::Params`
  intensity per view. The tonemap operator is a 9th push float (agx flag) with a CM1a clamp branch
  (saturate + sRGB OETF) added to the shader (AgX stays default). **Needs on-screen verification**
  of TAA/FXAA, Clamp-vs-AgX, and SSR.
- **Phase 3 SHIPPED (0f852fb) — editor viewport "show flags".** `ViewPostOverride` (render.api:
  `disablePost` master + per-effect `disableBloom`/`Ao`/`Ssr`/`Aa`) is an optional `RenderScene`
  param (mirrors `cameraOverride`), applied via `ApplyViewPostOverride` to the resolved per-view
  post before the `needsMotion` finalize; ephemeral, never serialized. `ScenePage` has a "Post"
  viewport-toolbar button opening a checkable show-flags menu bound to a page-local override.
- **Phases 1 + 2a + 2b + 3 done:** the whole post stack is authored per scene, applied per view,
  with editor viewport overrides. Remaining (breadth): phase 4 (color grading LUT / DoF / vignette
  / motion blur / auto-exposure), phase 5 (per-camera overrides + post-process volumes).

## Problem

The post stack exists and runs per view — tonemap/AgX, bloom, AO (GTAO/SSAO), SSR, TAA, FXAA
([Render/*Pass.cppm](../../Code/Draconic/Render/)) — but every knob is **hardcoded in the compose
path**, not authored data:

- Tonemap exposure is *fixed at 1.0* (`TonemapShaders.cppm`: "Exposure is fixed at 1.0 for now").
- Bloom `threshold = 1.0`, `knee = 0.5` (`BloomPass.cppm`).
- GTAO `radius = 0.5`, `intensity = 1.0`; AO mode is a literal (`AoPass.cppm`, `AoMode` Off/GTAO/SSAO).
- TAA `blendFactor = 0.97`, `varianceGamma = 1.25` (`TaaPass.cppm`).
- SSR `intensity = 1.0`, `thickness` (`SsrPass.cppm`).

So an artist can't tune a level's look, nothing is **serialized** with the scene, and nothing **ships**
to the runtime player. We need post-processing to be authored content: editable in the editor,
persisted, and read identically by the runtime.

## Decision: per-scene config, applied per-view

(See the discussion this doc formalizes.)

- **The authored config is per-scene** — the "look" of a level (exposure, tonemap operator, bloom, AO,
  SSR, anti-aliasing choice, later color-grading / DoF / vignette / fog). It is content, saved with the
  scene, shipped to the runtime.
- **It is applied per-view** — each `RenderView` resolves the effective config and owns the transient
  per-view state that already lives there (TAA color-history ping-pong, jitter, viewport sub-rect via
  `ViewSettings`). The post passes already render into each view's sub-rect (split-screen-safe).
- **The editor viewport gets per-view, non-persistent overrides** — display toggles ("disable post",
  "no TAA/DoF") for editing clarity, which must never be written back into the scene asset.

One authored config; resolved and applied per view; ephemeral editor-view overrides on top.

## Where it lives

Scene-level render config is already an established pattern: `EnvironmentSettings` is a reflected struct
on a render `SceneSystem` (`EnvironmentSystem`) exposing `SettingsType()/SettingsId()`
([Components.cppm](../../Code/Draconic/Render/Subsystem/Components.cppm)). The editor auto-surfaces every
scene system's settings — `InspectorView::BuildSceneSettingsSections()`
([InspectorView.cppm](../../Code/Draconic/Editor/Scene/InspectorView.cppm)) walks `SettingsType()` and
builds an inspector section, edited through undoable `EditContext::SetSceneSettingProperty` commands and
serialized with the scene; the renderer extracts it per frame (`ExtractEnvironmentInto`,
[Extract.cppm](../../Code/Draconic/Render/Subsystem/Extract.cppm)).

**Add a reflected `PostProcessSettings` on a render `SceneSystem`** and it slots into all of that with
no new editor UI: it appears as a "Post Processing" section in the scene-settings inspector, is
undoable, serializes with the scene, and is extracted per frame into each view.

Two placements:

1. **Extend `EnvironmentSettings`** — least work, but conflates lighting/environment with post.
2. **A dedicated `PostProcessSettings`** with its own `SettingsType()/SettingsId()` (on a
   `PostProcessSystem`, or a second settings block on the render subsystem) — its own inspector section,
   clean separation. **Recommended.**

## Data model

A grouped, reflected value struct — each effect has a master enable plus its parameters, mapped 1:1 to
the pass that already consumes them (so this is drop-in: the pass reads `settings.X` instead of a
literal). Defaults = today's hardcoded values, so behavior is unchanged until edited.

- **Exposure / tonemap**: `exposure` (EV or linear multiplier; default 1.0), `tonemapOperator`
  (enum: `Clamp` / `AgX`, matching CM1a/CM1b), later auto-exposure params.
- **Bloom**: `enabled`, `threshold` (1.0), `knee` (0.5), `intensity`.
- **Ambient occlusion**: `mode` (`Off`/`GTAO`/`SSAO`), `radius` (0.5), `intensity` (1.0).
- **Screen-space reflections**: `enabled`, `intensity` (1.0), `thickness`, quality.
- **Anti-aliasing**: `aaMode` (`Off`/`FXAA`/`TAA`) — a single enum, **not** two bools, since TAA and
  FXAA are mutually exclusive (the FXAA path is the no-TAA fallback). TAA sub-params (`blendFactor`
  0.97, `varianceGamma` 1.25) when `aaMode == TAA`.
- **Future groups**: color grading (LUT `resource::Ref<Texture>`, like `EnvironmentSettings::skyTexture`;
  lift/gamma/gain), depth of field, vignette, motion blur, fog.

Reflect it with `DRACONIC_REFLECT_VALUE` + `.Property<...>(...)` exactly like `EnvironmentSettings`, so
the inspector renders sliders/enums/resource-pickers automatically.

## Runtime flow

1. **Extract** — `ExtractPostProcessInto(scene, out)` copies `PostProcessSettings` into the extracted
   scene each frame (mirrors `ExtractEnvironmentInto`).
2. **Resolve per view** — a `RenderView` takes the scene config as its base and layers any per-view
   overrides (editor viewport flags; later per-camera overrides) into an effective config.
3. **Passes consume the resolved values** — `TonemapPass`/`BloomPass`/`AoPass`/`SsrPass`/`TaaPass`/
   `FxaaPass` read `effective.X` instead of literals. Per-view transient state (TAA history, jitter,
   sub-rect) stays exactly where it is now — the config only supplies *parameters*, not state.

The `aaMode` enum drives which AA pass the graph declares (and, for TAA, whether jitter + motion vectors
are enabled for the view — already a per-view concern).

## Editor exposure

- **Automatic inspector section** — once `PostProcessSettings` is reflected and hung on a system, the
  scene-settings inspector shows a "Post Processing" section with foldout groups per effect. No bespoke
  UI. Edits are undoable and take effect next frame (extract re-reads it) → **live preview** in the
  viewport.
- **Widgets come from reflection** — floats → sliders (with sensible ranges), enums → dropdowns
  (`aaMode`, `tonemapOperator`, AO `mode`), resource refs → asset pickers (color LUT), like the sky
  texture today.
- **Grouping** — either nested reflected sub-structs (Bloom, AO, SSR, …) or a naming convention the
  inspector groups on; nested structs read best.

## Editor viewport overrides (per-view, non-persistent)

A viewport **"Post" show-flags menu** (à la Unreal's *Show → Post Processing*): per-editor-view toggles
that write to the view's `ViewSettings` (already bound per `RenderView`), **not** to the scene:

- Master "disable post" (see raw HDR/tonemap-only), "disable TAA" (crisp while inspecting), "disable
  DoF/vignette" (unobstructed editing).

These are ephemeral editor display state — never serialized, never in the scene asset. They cleanly
separate "the game's authored look" from "how I want the editor to display right now."

## Serialization & shipping

`PostProcessSettings` serializes with the scene (versioned payload, same path as `EnvironmentSettings`).
The runtime player reads the same extracted settings through the same render subsystem — **zero editor
dependency**, identical look in-editor and shipped.

## Future: localized looks

Per-camera overrides and **post-process volumes** (Godot `WorldEnvironment` / Unreal `PostProcessVolume`,
blended by proximity/priority) are the natural next layer for localized grading. The per-scene block is
the base layer and lands first; volumes blend *over* it. Defer until there's a concrete need. Also later:
auto-exposure (histogram), depth of field, motion blur, color-LUT grading.

## Phased plan

1. **`PostProcessSettings` + inspector + serialize** — reflect the struct (exposure/tonemap, bloom, AO,
   SSR, AA-mode) on a `SceneSystem`; defaults = current hardcoded values; it appears in the scene-settings
   inspector, undoable and saved. *(No visual change yet — pure plumbing + authoring surface.)*
2. **Extract + pass wiring** — `ExtractPostProcessInto`; passes read resolved values instead of literals.
   Now editing the inspector changes the image live and the look ships.
3. **Viewport show-flags** — per-view override menu on `ViewSettings` (disable post / TAA / DoF).
4. **Grow the model** — color grading (LUT), DoF, vignette, motion blur; auto-exposure.
5. **Localized** — per-camera overrides, then post-process volumes.

Phases 1–2 deliver authored, serialized, live-previewed, shippable post-processing with mechanisms
already in the codebase; 3 adds editor ergonomics; 4–5 grow breadth.

## Open questions

- Placement: extend `EnvironmentSystem`, or a dedicated `PostProcessSystem`/settings block? (Leaning
  dedicated, for a clean "Post Processing" section and separation from lighting.)
- Exposure units: linear multiplier (matches the current push) or photographic EV/stops (better artist
  mental model, needs a conversion)? (Leaning EV with a documented mapping.)
- Where per-view overrides live: extend `ViewSettings`, or a parallel editor-only view-state struct that
  the viewport owns? (Leaning `ViewSettings` since it is already the per-view bind payload.)
- Do split-screen / multiple in-scene cameras need independent authored post now, or is per-scene + the
  future per-camera-override layer enough? (Assume the latter until a case appears.)
