# Sound cue: how should cooking an EMPTY / invalid cue behave?

Status: DESIGN QUESTION for Fable (Opus, 2026-08-18). Origin: the week-2026-08-15
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
