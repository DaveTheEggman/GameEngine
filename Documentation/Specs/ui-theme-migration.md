# UI theme migration - built-in themes to .sss + the consistency pass

> STATUS: ACTIVE 2026-08-16 (user un-deferred P3 and widened it). Fable-executed.
> Existing-implementation check (per the CONVENTIONS spec rule): the full scoping
> investigation lives in Documentation/Backlog/ui-core-audit.md ("P3 SCOPING
> INVESTIGATED") - the SSS parser vocabulary is ~95% sufficient, breeze.sss
> covers every icon slot, TexturedTheme embeds no images (excluded, stays C++).

## Goals (user, 2026-08-16)

1. Migrate the built-in C++ themes to embedded `.sss` (Dark -> Light ->
   RoundedDark; GameTheme/GameLightTheme come free as palette reskins).
2. YES to the toolkit theme as SSS: toolkit control types become selectable
   and ToolkitThemeExtension's rules move to a `toolkit.sss` fragment.
3. THE CONSISTENCY PASS: migration is the moment to make theming "nice and
   consistent - proper padding, margin, font-size, colors. Most don't look
   bad so far, but could look better." Concretely:
   - ONE spacing scale (4/6/8/12/16-style ramp) - paddings stop being
     per-control accidents ({12,8} here, {8,6} there, {6,4} elsewhere).
   - ONE type ramp (e.g. 12 caption / 14 body / 16 emphasis) applied
     deliberately, not 14.0f-vs-16.0f defaults by historical accident.
   - COLORS derive from the palette (state functions / state-colors ramps),
     replacing the ~70 hardcoded x/255 literals per theme file; disabled/
     hover/pressed always derived, never eyeballed.
   - The UA DEFAULT SHEET (audit P3 item) ships the normalized defaults in
     ONE place; per-call-site control defaults (DefaultStylePadding etc.)
     become overrides of last resort, and the control state-ladder fallbacks
     route through StateListDrawable rules instead of Palette::Compute calls.
   Rule for the pass: consistency changes are DELIBERATE and reviewed
   against screenshots per phase - this is design polish, not blind
   normalization; anything that looks worse gets a per-control exception.

## Phases (battery green per phase; editor/sandbox visual verify per theme)

- **P0 - SSS capability gaps** (closes a live bug first):
  a. `svg(name[, tint])` falls back to ThemeIconSet::Acquire for the 10
     builtin glyph names (kebab-case map) - fixes the SILENT NULL in cooked
     game themes AND makes SSS icons the shared baked instances; the UI
     cook FAILS on an unresolvable svg/image name instead of nulling.
  b. `rounded-rect()`/`state-rounded()` accept per-corner `radius=a b c d`.
  c. Color functions `hover($c)`, `pressed($c)`, `disabled($c)`,
     `focused($c, $accent)` delegating to Palette::Compute*.
  d. `RegisterToolkitTypes()` - toolkit controls enter UITypeRegistry so
     SSS can select them (mirrors RegisterBuiltins; count tripwire).
- **P1 - dark.sss.** Authored as a real in-tree .sss (also usable as an
  asset template later), embedded at build; DarkTheme::Create(palette)
  keeps its signature (SetPalette + Load(embedded)). A RULE-DIFF PARITY
  TEST proves the parsed sheet matches the C++-built sheet BEFORE the C++
  body is deleted; then the consistency pass edits dark.sss deliberately
  (parity test retires into a looks-right visual verify).
- **P2 - light.sss (exercises tinted shared glyphs) + rounded-dark.sss
  (per-corner radii; the EDITOR's live theme - user visual verify gate).**
- **P3 - toolkit.sss** merged via the @import/MergeFrom mechanism;
  IThemeExtension stays as the hook (editor's post-parse code overrides
  keep working on the parsed sheet). BUILT 2026-08-16: TWO fragments
  (`UI.Toolkit/Themes/toolkit-dark.sss` + `toolkit-light.sss`) because the
  legacy `isDark` branch derives in DIFFERENT directions per control (dark
  darkens harder or lightens where light darkens gently - not expressible
  as a variable swap); the extension keeps the one-line predicate
  (`p.Background.r < 0.5f`), parses the matching fragment, MergeFrom into
  the theme sheet. Legacy body = `ApplyLegacyForParity` (oracle + parse-
  failure belt). NEW parser property `background-color:` stores Background
  as a raw COLOR for ResolveStyleColor consumers (ToastCard) - plain
  `background:` stays the drawable path. Parity comparator extracted to
  UI.Tests/ThemeParityHelpers.h (shared); 3 toolkit gates (Dark / Light /
  GraphiteOrange) with an anti-vacuousness guard (fragment must parse
  non-empty so a belt fallback can't fake parity).
- **P4 - the UA default sheet + control fallback cleanup** (state ladders
  -> StateListDrawable; Palette::Compute calls leave control draw code).

## Stays C++ (from the investigation - accepted)

ThemeIcons SVG strings + ThemeIconSet bake machinery (the backing store the
names resolve to), ThemePalette presets, window clear color, font setup,
the editor's post-parse injected overrides, TexturedTheme (image-factory
API for callers with real images; not a shipped look).

## Acceptance

Per phase: battery both compilers; the parity test green until the
consistency edits intentionally supersede it; per-theme screenshots
compared (user verify for the editor theme); the svg-null cook-fail has a
regression test; final state = Dark/Light/RoundedDark/Game* themes are
.sss text, C++ theme bodies deleted, toolkit styled via toolkit.sss,
consistent spacing/type/color ramps documented at the top of each .sss.
