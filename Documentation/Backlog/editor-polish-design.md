# Editor Polish — UX cleanup backlog

Status: **backlog (2026-07-24).** Started from a firsthand UX pass (Draconic vs. LunarSong side-by-side).
Living checklist — add items as they surface. Grounded in code (file:line), not guesses.

## Framing

The coverage gap is closed ([editor-pages-gap.md](editor-pages-gap.md) — all asset pages + code editor
+ autocomplete + debugger landed). What remains is **polish**, and the LunarSong comparison sharpened
what to aim for:

- **Draconic's edge is behavioral stability** — in side-by-side testing LunarSong looked more polished
  (icons, crisp Slug text, SDF chrome) but hit real defects: wrong clipping, panel/element content
  disappearing, "singleton" windows opening duplicates (old instance cleared), panels sized too small
  for their content. **Do not regress into that class of bug** — it's the thing we're currently better
  at, and it's easy to lose while chasing polish.
- **Prefer the simpler side.** The editor already has a lot going on; polish here means *organizing and
  calming*, not adding surface. Don't out-clutter the competition.
- Two concrete rough edges named in the pass: the **File menu** is too large/disorganized, and the
  **component-add menu** is an unorganized PascalCase list. Both below.

---

## P1 — Component-add menu (highest UX value, cheap, reflection-leveraged)

**Symptom:** `[+ Add Component]` is a flat, unorganized list showing raw **PascalCase code type names**
(`MeshComponentManager`-style), which is hard to scan and leaks implementation naming to users.

**Location:** `InspectorView::ShowAddComponentMenu` ([InspectorView.cppm:565](../../Code/Draconic/Editor/Scene/InspectorView.cppm)) — "lists the scene's managers" (module header note). It iterates the scene's component managers and labels each by type name.

**Fix (same reflection play as the bound-API autocomplete):**
1. **Display names, not type names.** Prefer a type-level display name; fall back to inserting spaces
   into PascalCase (`MeshComponent` → "Mesh Component", strip a trailing "Component"/"Manager"). The
   property inspector already resolves `DisplayName()` for rows — reuse/extend that at the *type* level.
2. **Categorize into submenus** by subsystem — Rendering / Physics / Audio / Animation / Particles / UI /
   Scripting. Derive the category from the component's registering module/namespace (`TypeInfo` carries
   `namespaceName`), or add a lightweight `[Category("…")]` type attribute if namespace grouping is too
   coarse. Render the add-menu as submenus per category.
3. **(Optional) filter box** at the top — the `CodeEditView`/search infra exists to lean on.

**Effort:** small. **Portability:** the pattern maps directly onto Sedulous (comptime reflection makes
category/display attributes even cleaner). *Design carries even if the code doesn't.*

**Open:** confirm whether `TypeInfo` needs a `displayName`/`category` field added, or whether
namespace-derivation + a spaced-PascalCase fallback is enough for v1 (lean: fallback first, attribute
later only if needed).

---

## P2 — File menu is overloaded

**Symptom:** too many unrelated items crammed into one menu; organization could be better.

**Location:** `ApplicationImpl.cpp` menu bar ([:2145+](../../Code/Draconic/Editor/App/ApplicationImpl.cpp)).
Current **File**: `New ▸` (creators submenu) · Save · Save As… · Save Layout · **Project Settings…** ·
**Preferences…** · **Export…** · **Manage Templates…** · Exit. A **Build** menu already exists
separately (Cook All / Rebuild All), plus Edit (Undo/Redo), Game (Play / Play New Instance), View
(Reset Layout), Help (About).

**Fix — move the project/build/prefs items out of File:**
- **File** keeps document/app essentials: `New ▸`, Save, Save As…, (sep), Save Layout, (sep), Exit.
  *(Add Open Project / Recent Projects here if/when they exist.)*
- **Preferences…** → **Edit** menu (the conventional home; it's per-user, not per-document).
- **Project Settings…**, **Export…**, **Manage Templates…** → a new **Project** menu (or fold Export
  + Manage Templates under the existing **Build** menu, since they're build/dist concerns; put Project
  Settings under Project/Edit).

That roughly halves File and puts each item where users expect it. **Effort:** trivial (menu wiring).

---

## P3 — General polish backlog (add as found)

- **Icons / visual crispness.** LunarSong reads crisper (Slug text + SDF chrome + icon set). The
  **MSDF font path** is proving out (VGSandbox works; UI integration behind `kUseDistanceFieldFonts`,
  [21678b0a]) — flipping that on is the biggest single crispness win and is already in flight. An icon
  pass (consistent set, proper sizing) is a separate, lower-priority item.
- **Docking feel.** LunarSong's docking felt nicer in testing — worth a focused compare of drag/drop
  target affordances, tab strips, and split ergonomics against `Toolkit/Docking`.
- **Panel default sizes.** Watch our *own* defaults — the LunarSong "settings in a tiny scrolly panel"
  bug is a default-size/dock-placement failure (not a layout-engine one). Make sure content-heavy
  panels (inspector, settings, asset browser) open at sensible sizes.

---

## Regression guardrails (protect the stability edge)

These are the LunarSong defects we currently *don't* have — keep it that way as polish lands:
- singleton windows/pages must **focus, not duplicate** (we do this: `m_gamePage` focus-don't-reopen);
- panel/element content must not **disappear on redraw/clip** (watch clipping rects on scroll/resize);
- panels must **size to their content** or scroll cleanly, never clip silently.
Where practical, cover these with the interaction tests (and consider a record/replay harness later —
LunarSong's `UIReplay` is a good model for catching exactly these regressions deterministically).

---

## Order

1. **Component-add menu** (P1) — best value/effort ratio, reflection-driven.
2. **File menu reorg** (P2) — trivial, immediate clarity.
3. **MSDF UI flip** (P3) — biggest crispness win, already in flight.
4. Icons / docking feel / panel defaults — as capacity allows.
