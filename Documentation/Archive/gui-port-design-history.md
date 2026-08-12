# GUI (eepp-derived) - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/gui-port.md
> Track: [[gui-port]]

NON-AUTHORITATIVE. The eepp reference analysis behind the experimental GUI slice. Present-tense truth is
`Systems/gui-port.md`; the full original doc (the phase-by-phase plan, all eepp file mappings) is in git
at the P0 commit 3b92560d. Kept for the "why".

## Source

`/home/robert/Dev/CPP/eepp` (MIT - clean, no copyleft). Namespaces `EE::UI` / `EE::Scene` /
`EE::Graphics`.

## Why progressive-slice, not wholesale-lift

A wholesale lift of `include/eepp/ui` + `src/eepp/ui` (186 hdrs / 177 srcs, ~118k LOC) is a trap:
- **Dependency cone is most of the engine.** ui includes (by count): graphics x202, system x161,
  scene x75, core x62, window x52, math x34, network x18, audio x2.
- **No renderer seam exists.** ~19 files draw straight through `GlobalBatchRenderer` /
  `Graphics::Primitives` / immediate-mode GL. There is nothing to swap - the seam must be invented.
- **Constraint clash.** eepp is STL + exceptions + RTTI + raw-pointer ownership + its own scene graph;
  the engine is named modules, `-fno-exceptions`/`-fno-rtti`, no-STL APIs, own containers, UTF-8,
  `Object` reflection. A wholesale lift ports all of that at once with zero green feedback until the end.

## The decision

A FRESH `experimental.gui` built bottom-up on engine infra (VG + Fonts + Core + viewport-input),
progressively slicing eepp's design (borrow its VG/Drawable + CSS recipes, not its code). The Sedulous
`foundation.ui` port stays the real UI; GUI is a separate, parked, experimental module. Text shaping
(HarfBuzz + bidi, eepp's unique strength) is deferred until rich-text widgets need it - the parked
`foundation.ui` proved the no-STL/-fno-exceptions/Object pattern works.
