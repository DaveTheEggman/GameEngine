# Editor polish backlog

> ARCHIVED 2026-09-01: still-open items live in Documentation/Plans/week-2026-09-05.md
> ("Backlog folder absorbed"). This archive keeps the full detail.


Size: M (a set of S items; land separately). Modules:
`Code/Draconic/Editor/*`. Context: memory `editor-polish-track` - P1
(component-menu attributes) and P2 (menu reorg) shipped; these are the
remaining items. Theme REAUTHORING was dropped: after the double-decode fix
(16107056) the user confirmed themes render as authored.

## 1) Import dialogs audit

Sweep every import/export dialog (model importer, texture import, export
presets) for: missing labels, fields not persisted between opens, missing
validation (bad paths accepted silently), inconsistent button order/naming
vs the rest of the editor. Fix mechanically; keep a list of what changed in
the PR. New components/fields discovered to need inspector entries follow the
dispatch-entry rule (memory `inspector-ref-picker-table`).

## 2) Theming gaps audit

Find controls still using hardcoded colors instead of theme palette lookups
(`ThemePalette.cppm` roles): grep editor + toolkit views for Color literals,
classify (intentional accent vs gap), route gaps through the palette. Do NOT
change the palette values themselves (user checked them post-decode-fix).
List intentional literals in the PR so the next audit skips them.

## 3) Authored-SVG icons

The icon bake pipeline (#122, Godot-style, shipped) renders SVG sources into
crisp atlas icons. Several editor spots still use placeholder glyphs or
squares. Author simple SVG icons for the worst offenders (dock tabs, toolbar
toggles, component-menu categories), run them through the existing bake
pipeline (recipe in memory `vg-quality-track`), and wire them via ThemeIcons.
Constraint: editor font renders codepoints <= 255 only - icons must come from
the atlas, never from exotic codepoints (memory `editor-font-glyph-range`).
SVG gradients are supported since e0029d11 but icons stay FLAT when tinted
(tint overrides gradients by design) - author flat, tintable icons.

## 4) Docking feel

Known annoyances to fix in the docking host (`draconic.ui` docking + editor
integration; memory `ui-on-runtime`):
- Dock-target hit zones are small; enlarge the drop indicators.
- Dragging a tab out should show the floating preview under the cursor
  (drag-follow exists for OS floats; verify the indicator path).
- Re-dock hysteresis: avoid flicker when hovering near a splitter boundary.
Each fix needs a UI test where the logic is factorable (hit-zone math is), and
user verification for feel.

## 5) Panel sizes

Persist per-page panel split positions (hierarchy width, inspector width,
console height) in EDITOR settings (EditorSettings.cppm section - follow the
both-registrations rule from CONVENTIONS) keyed per page type, restored on
open. Defaults today are hardcoded; keep them as the fallback. Rule: this is
editor-only state - it must live in an editor settings section, never on
asset/runtime types.

## Acceptance

Per item: both compilers green, doctest coverage for factorable logic, user
visual pass. Items are independent - do not batch into one giant commit.

---

## State (appended 2026-08-03; original content above is unchanged)

**PARTIAL.** P1 (component-menu attributes) + P2 (menu reorg) + the icon bake
pipeline (#122) shipped; the Project Settings/Preferences scrollview (c5dfabeb)
also landed. REMAINING: the remaining polish items (docking, panel sizes, icons).
