# Reflection track - working-log history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/reflection-track.md
> Track: [[reflection-track]]

NON-AUTHORITATIVE. The working log (the measured gap, the blockers, and the Opus/Fable decisions) behind
the reflection coverage sweep. Present-tense truth is `Systems/reflection-track.md`; the full original
doc is in git at the P0 commit 3b92560d. Kept for the "why".

## The measured gap (2026-07-28)

The reflected island existed exactly where a consumer had demanded it (the scene inspector's components,
the replication field codec, the script backends' type harvest); everything else stayed identity-only,
so new consumers (the generic asset page, the particle inspector) had to be built WITHOUT reflection.
Census at the time: ~12 intrusive `REFLECT_MEMBERS`, ~46 `REFLECT_VALUE`, ~25 `REFLECT_ENUM` (of ~261
enums), and ~329 identity-only `RTTI_DEFINE_OBJECT`. The directive: "too much of the codebase is not
reflected - go reflect the useful things so they are usable for scripting and tooling."

## Blocker 1: nested non-copyable Object members (2026-08-03)

The flat P1 assets + the base `Asset::fileName` shipped, but the remaining P1 assets wrap a domain
source struct BY VALUE, and reflecting through it needed a nested-member facility. Fable's decision
(Q3): land the mechanism NOW (a Nested property kind; the generic page recurses; get returns a Variant,
set returns a clear error Status, not silent Ok), close P1 pragmatically at flats + base fileName
(shipped) + the nested mechanism, and let the nested (mesh) assets fold into P2's opening. An adjacent
`ComputedProperty` kind (a getter-backed property, NOT the Nested kind) was added alongside.

## Blocker 2: two container primitives for the particle sweep (2026-08-03)

The particle-module reflection (P2) hit the limit of the existing primitives - 15 of 20 modules
reflected, the rest needing container support. Two primitives were added: a C-array flat view and the
`Array<UniquePtr<T>>` poly container. Neither blocked anything shipped (the particle page is bespoke;
15/20 modules already reflected), so they landed as a "reflection containers" follow-up. The wiring step
(full effect traversal proof) landed at commit f1713d32.

## Collections-in-scripts lift (unit-by-unit)

- Unit 1 (4451f118): constructor-less reflected types bind as RETURNED HANDLES.
- Unit 2a (349b612c): container MEMBERS bind as ops on the owner (`NAME_count` / `NAME_at(i)` / ...).
- Unit 2b: a `Variant` BORROW mode for nested-value / non-Object element handles (Fable review;
  Correction 1: swap-removes mean the element address must be recomputed, never cached - hence the
  reflection mutation generation guard).

All of the above shipped 2026-08-04 as the full collections-in-scripts lift across both backends plus
the generic list editor.
