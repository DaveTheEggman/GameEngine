# Sound cue: how should cooking an EMPTY / invalid cue behave?  (archived)

> Status: ARCHIVED - fully built. Non-authoritative: this is the original build spec, kept
> as the record of what was built and why; present-tense truth is the code + tests.

Status: RULED - build A + D + a health check (Fable 2026-08-18, call delegated
by the user; graduated Ideas -> Specs). Was: DESIGN QUESTION for Fable (Opus,
2026-08-18). Origin: the week-2026-08-15
Audio UAT cluster, item 1: "a freshly-created cue immediately fails to cook
('no playable variant') - poor UX; an empty cue should not auto-cook / should
message clearly." Poses the cook contract for a not-yet-authored cue.

## The finding (verified)

`SoundCueAssetBuilder::Build` (Code/Pipeline/Audio.Pipeline/AudioAsset.cppm:743-750)
HARD-FAILS the cook when the cue resolves to zero playable variants:

    // Validate: a cue with no playable variant is a broken trigger - fail the cook.
    if (source.variants.IsEmpty())
    {
        LOG_ERROR(u8"Audio", u8"sound cue has no playable variant ... - cook failed");
        return Status{ErrorCode::InvalidArgument};
    }

A SoundCueAsset is created empty (all `kSoundCueSlotCount` slots nil), so the very
first cook after "New > Sound Cue" fails with a red error in the log / cook driver -
before the user has had any chance to assign a clip. That is the poor UX.

The SoundCuePage's Audition already messages cleanly ("No playable variant.") when
you try to play an empty cue - that part is fine. The problem is the COOK erroring on
a brand-new asset.

## The decision

What is the right contract for cooking a zero-variant (or otherwise-unplayable) cue?

- **A. Cook to a valid EMPTY product.** Write a `SoundCueSource` with zero variants;
  the cook SUCCEEDS. Playing it is a silent no-op. The SoundCuePage shows a clear
  inline "empty cue - assign at least one clip" status. Pro: no error on fresh
  assets; the asset always has a product; "messages clearly" via the page. Con: a
  cue left empty by MISTAKE is silently a no-op at runtime (no diagnostic).
- **B. Skip the cook for empty cues.** The builder / cook driver treats zero-variant
  as "nothing to cook" (soft skip, not an error). Con: no product exists; every
  consumer binding the cue must handle absent-product, and a genuinely-broken cue is
  indistinguishable from a not-yet-authored one.
- **C. Defer the cook.** The editor does not (auto-)cook a cue until it has >=1
  variant. Matches "should not auto-cook" literally. Con: needs the cook trigger
  gated on validity, and the asset stays uncooked until authored - anything that
  force-cooks (Cook All) still has to decide what to do with it.
- **D. Keep the failure semantics, fix only the UX.** The cook still reports the cue
  as unconfigured, but (1) the SoundCuePage shows a clear inline "assign at least one
  clip to cook" message, and (2) a freshly-created, never-authored cue does not
  surface as a scary red FAILURE in the browser / cook log - it reads as an
  "unconfigured / draft" state until first authored.

## Open sub-questions

1. Is a zero-variant cue EVER legitimately valid (an intentionally-silent trigger),
   or is empty ALWAYS a mistake? The answer decides whether A's silent-no-op is
   acceptable or dangerous.
2. What actually triggers the first cook of a fresh cue - creation auto-cook, a
   background Cook-All, or the first bind? (Determines whether C is even reachable
   without touching the cook driver.) I did not find a cue-specific auto-cook on the
   creator path; the failure appears to come from the general cook driver processing
   the new asset. Confirm the desired trigger behavior with the answer.

## Tentative recommendation (Opus, for Fable to challenge)

A hybrid of **A + D**: the cook produces a VALID empty product (no hard failure, so
no red error the instant you create a cue), AND the SoundCuePage shows a clear inline
"empty cue - assign at least one clip" status so it reads as an unfinished draft, not
a working sound. A fresh cue becomes a benign not-yet-authored state that never blocks
and always messages. If Fable wants empty cues to REMAIN an error (to catch the
forgot-to-assign mistake at runtime), option C (defer + gate the cook) is the
alternative, at the cost of cook-driver plumbing.

Not blocked on this: the other two Audio items (audition can't pause/stop + stacks
overlapping playback; AudioSource has too many fields) are independent and I am doing
them in parallel.

## BUILT (Opus, 2026-08-18) - all three parts landed

- **A (builder)**: SoundCueAssetBuilder cooks a zero-variant SoundCueSource for an empty cue
  instead of failing; Version 1 -> 2. Test: empty cue cooks OK -> empty product; ResolveSoundCue
  on it -> variantIndex < 0 (Audio.Pipeline.Tests, 218 assertions).
- **D (page)**: SoundCuePage shows a persistent "Empty cue - assign at least one clip." hint,
  cleared once a clip is assigned (RefreshEmptyHint, driven from RefreshSlot).
- **project_health**: emptyCues warning - cues with no clip in any slot are reported (does NOT
  flip `sound`; an empty cue is a valid buildable draft). TESTED in the Integration.Mcp golden
  flow (register tools -> project_create/open -> call project_health -> assert the JSON): an empty
  cue shows in emptyCues with sound=true, a filled cue does not. (An earlier note here wrongly said
  MCP had no test harness - corrected: Integration.Mcp is exactly that harness.)

Both compilers green throughout.

## FABLE RULING (2026-08-18): A + D's page messaging + a project_health warning

The hybrid recommendation is right, with one addition that resolves
sub-question 1's tension properly. Build:

1. **A - the cook SUCCEEDS with a valid empty product.** Remove the hard-fail;
   an empty cue writes a SoundCueSource with zero variants. This is SAFE with
   zero runtime changes - verified: ResolveSoundCue already answers
   variantIndex = -1 for an empty eligible set and every consumer (component
   play, one-shot, audition) already guards `pick.variantIndex >= 0`. The
   hard-fail in the builder was the ONLY place that treated empty as an
   error; the runtime was designed for it all along. This also matches the
   codebase convention the fail violated: freshly-created assets (material,
   clip, scene, navigation zone...) are cookable by construction - the cue
   was the outlier. And it un-poisons Cook All / export: a draft cue must
   never block a ship.
2. **D's UX - the SoundCuePage shows the draft state inline**: "empty cue -
   assign at least one clip" (persistent status on the page, same voice as
   the audition's existing message). No red anywhere for a never-authored
   asset.
3. **The addition - project_health reports empty cues as a WARNING.** This is
   the answer to sub-question 1: yes, an intentionally-silent cue is a
   legitimate thing (placeholder hookups, silence-overrides), so runtime
   stays a silent no-op - but the forgot-to-assign MISTAKE still gets caught,
   at the right layer: the audit surface (MCP project_health), not the cook.
   Cook = "is this buildable" (yes); health = "is this suspicious" (yes,
   warn). One check in the health tool: cues whose cooked source has zero
   variants.

Explicitly rejected:
- **B (skip, no product)**: pushes absent-product handling onto every
  consumer and makes broken indistinguishable from unauthored. Worst option.
- **C (defer/gate the cook)**: needs validity plumbing in the cook driver,
  and Cook All still has to answer the question - C collapses into A or D at
  the driver level while costing the most. The "should not auto-cook" phrasing
  in the UAT item is satisfied by A: the auto-cook stops FAILING, which was
  the actual complaint.

Sub-question 2 needs no further chase: with A, the trigger does not matter -
whatever cooks the fresh cue now succeeds.

Mechanics: removing the hard-fail changes builder behavior for the same input
(failure -> empty product). Per the standing rule, bump the builder's
Version() in the same commit so previously-failed cues re-cook into products.
Runtime playing an empty cue may LOG at DEBUG level (once per resolve is
fine) - never WARNING (intentional silence must not spam). Tests: builder
cooks an empty cue to a zero-variant product; ResolveSoundCue empty ->
variantIndex -1 (pin it explicitly if not already pinned); the health check
flags a zero-variant cue; the page status appears for an empty cue and clears
when a clip is assigned.
