# Settings: unknown-section passthrough  (archived)

> Status: ARCHIVED - fully built. Non-authoritative: this is the original build spec, kept
> as the record of what was built and why; present-tense truth is the code + tests.

Size: S. Module: `Code/Draconic/Foundation/Draconic.Settings/` (+ Editor.Core
consumers). No new dependencies.

## Context

`Settings::Load` (Settings.cppm) instantiates each section through the type
registry / serializable registry. Today an UNINSTANTIABLE section (unknown type
name, missing factory) aborts the WHOLE store at the first bad section - every
section after it in the file is silently dropped, and the next Save rewrites the
file without them. This is exactly how the editor lost its project list on
2026-08-01: `EditorUiSettings` had no `RegisterSerializable<>` factory, it
happened to be the FIRST section in the user's file, and every launch dropped
all settings after it (fixed for that type in 3c91c8a4, but the failure mode
remains for any future unknown section - e.g. opening a NEWER version's settings
file with an OLDER binary).

## Goal

Unknown or uninstantiable sections must be (a) preserved across a Load/Save
round-trip, (b) skipped without aborting the rest of the store, (c) surfaced as
a warning, never silently.

## Design

1. In the store's section loop, an uninstantiable section no longer returns an
   error for the whole store. Instead:
   - Capture the section's raw serialized bytes (its XML subtree / binary
     blob exactly as read) plus its type-name string into an
     `UnknownSection { String typeName; Array<u8> payload; }` list on the
     store.
   - Log ONE warning per unknown section (type name + why: unregistered vs no
     factory).
2. `Settings::Save` writes unknown sections back out verbatim, in their
   original relative order (append after known sections is acceptable if
   preserving exact order is intrusive - state which you did).
3. A section that IS instantiable but fails to deserialize its fields keeps the
   current behavior (that is data corruption, not version skew) - but must
   still not abort other sections; it should be reported and re-serialized from
   defaults. Confirm current behavior and align it.
4. The load result should distinguish "clean", "loaded with N unknown sections
   preserved" (Ok + warning log), and real IO/parse failure (error Status).
   Callers like `Editor.App/ApplicationImpl.cpp::LoadEditorSettings` (which now
   logs ERROR on non-NotFound failures) must keep working unchanged.

## Constraints

- Wire compatibility: a file with zero unknown sections must produce
  byte-identical output to today (do not perturb ordering or formatting for the
  known-section path).
- The XML settings encoder/decoder lives with the store; the raw payload
  capture must work for BOTH the XML and binary serializer backends
  (`draconic.xml.serialization` ISerializer). If binary passthrough is
  disproportionately hard, implement XML passthrough (the settings files are
  XML) and make binary loads skip-with-warning; say so in the PR.

## Tests (doctest, Draconic.Settings tests target; scratch in .test-scratch/)

- Round-trip: store with sections A, UNKNOWN (hand-authored XML for a type name
  that is not registered), B -> Load -> Save -> reload with the type NOW
  registered -> the unknown section's data is intact.
- Ordering: A, UNKNOWN, B all survive; B is not dropped (the regression that
  motivated this spec).
- Uninstantiable-first: UNKNOWN as the FIRST section must not affect A/B.
- Save-without-load-cycle: repeated Load/Save cycles do not duplicate or mutate
  the preserved payload.
- Status surface: unknown sections still yield IsOk() (with warnings), a
  malformed file yields an error.

## Acceptance

- The 2026-08-01 incident scenario (unknown first section) is impossible to
  reproduce: all other sections load, the unknown one survives the rewrite.
- Both compilers green; existing ProjectRegistryTests (instantiability +
  round-trip, Editor.Core.Tests) still pass unchanged.

---

## Opus: PUNTED to Fable (2026-08-03) - blocked on serializer-layer design

Attempted, then reverted (nothing landed). The XML side is straightforward and
was working; the blocker is the cross-backend UNIFORMITY requirement (user
directive: "XML serialized and binary serialized have to stay uniform - if
settings switched to binary tomorrow it has to work the same"), which turns this
into a shared-serializer-contract change rather than a Settings-local one.

### What was prototyped (reverted)

- New `ISerializer::RawRemainder(Array<u8>& blob)` virtual (default `false` =
  unsupported). Mode-dual: READ serializes the current object's not-yet-read
  remaining children into the blob and consumes them; WRITE re-injects a
  captured blob's content.
- `XmlSerializer` override: the DOM is self-describing, so it captures the
  section's remaining child elements (`dataVersions` + `payload` subtree) via
  `ToXml` and re-emits them by parsing (wrapped in a synthetic root) and MOVING
  the nodes into the write element (RemoveChild detaches without deleting;
  AppendChild re-parents). Verified building.
- `Settings`: an `UnknownSection { typeNamespace; typeName; payload }` list;
  Load captures unknowns via RawRemainder (warn, continue) instead of aborting;
  Save re-emits them after the known sections. Known-section path untouched ->
  byte-identical XML output, and old XML files load unchanged (the DOM makes
  passthrough format-version-free).

The XML-only result satisfied goal (a)/(b)/(c) and every acceptance bullet FOR
XML. It builds green.

### Why it is blocked (the real design decision)

`BinarySerializer` gives objects NO framing - "keys dropped; only arrays,
strings, and scalars carry a u32 length/count prefix" (BinarySerializer.cppm).
So an unknown section's payload (an object) is NOT self-delimiting: a build that
does not know the type cannot find where the section ends, so it can neither
skip NOR capture it positionally. This is exactly why the original code aborted.

To make binary PRESERVE unknown sections (uniform with XML), every section's
payload must become self-delimiting - i.e. length-framed, the pattern Scene
uses (`Serialize(ar, "data", blob)`, SceneResourceImpl.cpp streamVersion 2:
sub-serialize the payload to a MemoryStream, store it as a length-prefixed
blob; unknown -> keep/skip the blob). That is a BINARY FORMAT CHANGE. It is
SAFE (settings ship as XML; there are no binary settings files on disk to
break), but:

1. XML must stay inline + human-readable (the whole point of XML settings), so
   the two backends need DIFFERENT mechanisms: XML = DOM subtree; binary =
   length-framed blob. They cannot share one wire shape without making XML an
   unreadable hex blob.
2. Every section (known AND unknown) must be framed in binary - you cannot frame
   only unknowns, because the file was written by a build that KNEW the type and
   wrote it inline. So the framing has to be a permanent property of the binary
   section envelope, driven for both backends by one Settings code path.
3. The clean seam is a serializer capability - e.g. `BeginFrame()/EndFrame()`
   (binary length-prefixes the enclosed payload via a sub-stream; XML no-ops and
   relies on element boundaries) - which lives on the shared `ISerializer` /
   `Serializer` contract that Fable owns. That is a bigger, cross-cutting change
   than this spec's "S" sizing, and it touches every backend's framing model.

### Recommendation for Fable

Decide the framing design on the serializer contract, then this becomes
mechanical:
- Add `BeginFrame()/EndFrame()` (or equivalent) to `ISerializer`: XML no-op,
  binary length-prefixes the region (sub-stream + blob, no backpatch needed -
  the Scene pattern). Keep the `RawRemainder`-style capture for the XML DOM path.
- Settings drives BOTH through one framed path: write typeNamespace + typeName +
  BeginFrame + versioned payload + EndFrame; on read, frame lets an unknown
  section be captured/skipped uniformly.
- Confirm the binary-format-change is acceptable (no shipped binary settings,
  so it should be), and whether a store-level format version is wanted for
  future-proofing.

If an XML-ONLY scope is ever acceptable (settings are XML in practice), the
reverted prototype closes it immediately; the diff was ~145 lines across
ISerializer/XmlSerializer/Settings and all acceptance bullets passed for XML.

---

## Fable: framing design DECIDED (2026-08-03) - unblocked, build as follows

Opus' analysis is correct and complete: binary objects are not
self-delimiting, so uniform passthrough requires section framing in the
binary envelope, and that is a shared-serializer-contract change. Approved.
The decisions:

1. **Add the capability to the `Serializer` contract** as
   `BeginFramedRegion()/EndFramedRegion()` (full-name rule; "frame" alone
   collides mentally with render frames). Semantics exactly as proposed:
   - XML: no-ops - element boundaries already delimit; XML stays inline and
     human-readable (non-negotiable).
   - Binary: WRITE = sub-serialize the enclosed region to a MemoryStream and
     emit it as a u32-length-prefixed blob (the Scene `Serialize(ar,"data",
     blob)` pattern - no backpatching); READ = read the length, expose the
     region; an unknown region is captured or skipped as a blob by length.
   Default implementation on the base: no-op (backends without framing keep
   today's behavior; only Settings relies on it initially).
2. **Settings frames EVERY section** (known and unknown) through one code
   path: typeNamespace + typeName + BeginFramedRegion + versioned payload +
   EndFramedRegion. Unknown section: XML captures the DOM subtree (keep the
   reverted `RawRemainder` mechanism for that path); binary keeps the blob.
3. **Binary format change: approved.** No binary settings files have ever
   shipped - no back-compat shim. DO add a store-level format version to the
   binary settings envelope now (write v2, refuse v1-shaped streams with a
   clear error rather than misparsing) so the NEXT format change has a
   lever. XML files on disk load unchanged (verified by the prototype).
4. **Scope guard**: the framed-region capability lands for XML + Binary
   only; do not sweep other serializer users onto framing (Scene already
   does its own blob framing - leave it; converging Scene onto the new
   capability is a later cleanup, not this spec).
5. Re-sizing: M (serializer contract + two backends + Settings on top).
   Land in two commits: (a) BeginFramedRegion/EndFramedRegion + RawRemainder
   + serializer round-trip tests (framed known data, framed unknown data,
   nested regions, zero-length region, XML no-op equivalence); (b) the
   Settings passthrough re-applied on top with the original spec's
   acceptance tests run against BOTH backends.

The reverted XML prototype's shape (RawRemainder DOM capture, unknown-list
on the store, re-emit after known sections) is approved as-is for the XML
side - re-apply it, don't redesign it.

---

## Opus: IMPLEMENTED per Fable's design (2026-08-03)

Built in the two commits Fable specified.

**Commit 1 - serializer framed-region contract (c4c7076e):**
- `Serializer` gains `BeginFramedRegion()`/`EndFramedRegion()` (no-op default) +
  `RawRemainder(Array<u8>&)` (default returns false = unsupported). Named
  `BeginFramedRegion`/`EndFramedRegion` per Fable (full-name rule).
- `BinarySerializer`: real framing via a target-swappable `RedirectStream` + a
  frame stack. WRITE redirects the enclosed I/O into a `MemoryStream` sub-buffer;
  End emits it as `u32` length + bytes (the Scene `Serialize(ar,"data",blob)`
  pattern, no backpatch). READ reads the length, bounds a sub-buffer, and End
  discards any unread bytes (skip). `RawRemainder` moves the framed bytes.
  Nesting + zero-length handled.
- `XmlSerializer`: self-describing, so the frame markers stay the inherited
  no-ops; `RawRemainder` captures/re-injects the current scope's remaining
  element subtree (cross-document node MOVE is memory-safe - one global
  allocator, `RemoveChild` detaches without delete, `AppendChild` adopts).
- Tests: 6 binary (skip-by-length, read-inside, capture+re-emit-identical,
  nested, zero-length, no-frame-unsupported) + 2 XML (no-op equivalence,
  unknown-payload capture -> re-inject -> recover).

**Commit 2 - Settings passthrough + store format version (see follow-up commit):**
- Settings frames EVERY section (known and unknown) between
  `BeginFramedRegion`/`EndFramedRegion`. Unknown sections are captured via
  `RawRemainder` into an `UnknownSection { typeNamespace; typeName; payload }`
  list (rebuilt each Load), and re-emitted by Save after the known sections -
  no longer aborting the store. A section that a backend genuinely cannot
  preserve is dropped with a warning (never silently).
- **Store format version:** resolved the tension between "binary gets a version
  lever" and "XML files load unchanged" with a new
  `Serializer::IsSelfDescribing()` (binary=false, XML=true). Settings writes/reads
  a `formatVersion=2` field ONLY on positional (binary) backends and refuses a
  non-v2 binary stream; XML (self-describing) carries no version field, so files
  on disk are byte-unchanged and older files load. This satisfies both of Fable's
  requirements without a backend-type branch in Settings.
- Tests (both backends): the full acceptance set - author {A, X, B} -> load with
  X unregistered (X preserved, A+B load, store NOT aborted, B not dropped) ->
  re-save -> load with X now registered (X's data intact) - plus a repeated
  Load/Save cycle that does not duplicate/mutate the preserved payload.

Both compilers green: Core 28157+ (incl. 6 framed-region cases), XML 74, Settings
73 (incl. binary+XML passthrough). One deviation from the letter of the spec,
flagged for Fable: the format version is gated on `IsSelfDescribing()` rather than
written unconditionally, precisely so XML files stay unchanged (Fable's point 3).

---

## State (appended 2026-08-03; original content above is unchanged)

**SHIPPED** - the "PUNTED to Fable" section above is now historical. Fable
designed the cross-backend contract and it landed as two commits: the serializer
framed-region contract, then Settings framing + unknown-section list + binary
store format v2 (749a98ee). Both backends uniform; old files load unchanged.
