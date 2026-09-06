# Editor background jobs: EditorJobService and its two lanes

> Status: CURRENT
> Verified: 2026-08-12 @ 7e4ebae7
> Track: [[editor-track]]

`EditorJobService` (`Code/Editor/Editor.Core/JobService.cppm`, module `editor.core`) - owned by the
editor application, pumped once per frame from the app's update (`m_jobService.Update(...)`), exposed to
pages through `EditorContext::Jobs()` (may be null in headless/test contexts; callers must fall back to
synchronous work). It is THE structural way editor code gets work off the UI thread, with two
deliberately different lanes.

## Build lane - `Submit(title, work, onDone)`

For work that touches the content databases or the filesystem the cook reads (export, batch import,
reimport-all):
- One at a time - a submit while busy queues behind the running job (the editor build-lock: a cook/
  export/import never races the DBs).
- `IsBusy()` while running or queued, folded into the cook service's `MutationLocked`
  (`m_cookService.ExternalMutationLock`), so DB mutations and new cooks hold off until the job finishes.
- Progress through `JobContext` (fraction, named steps, log lines) rendered in the status bar;
  cooperative cancel via `CancelActive()`.
- `work` runs on a worker thread and returns a `Status`; `onDone(Status)` fires on the MAIN thread from
  `Update()`.

## Light lane - `SubmitLight(work, onDone)`

For short CPU-only side work whose results feed the UI (the FontEditorPage preview bake is the first
user; texture/mesh thumbnails, preview decodes, search indexing fit):
- Its OWN worker, concurrent with the build lane, deliberately OUTSIDE `IsBusy()` - that flag gates cook
  mutations, and a preview must never lock the build.
- No progress UI, no cancellation (light work finishes in fractions of a second; if it needs progress or
  cancel, it is not light - use the build lane). Queued lights run one at a time in submit order;
  `onDone` may chain another submit.
- `work()` runs on the light worker; `onDone()` fires on the MAIN thread from `Update()`. Results travel
  through state captured by both closures - the completion flag publishes the worker's writes, so no
  extra locking.

## Rules for light-lane users (learned from the font page)

1. **Never capture `this` without a lifetime guard.** The completion fires on the main thread, possibly
   AFTER the submitting page closed. Pattern: heap-allocate a slot owned by the closures; the object's
   dtor sets `slot->pageAlive = false`; the completion checks the flag before touching the object and
   always deletes the slot. Dtor and completion both run on the main thread, so this is ordering, not a
   race.
2. **Latest-wins for re-entrant requests.** If a new request arrives while one is in flight, stash it
   (one pending slot, overwritten freely) and submit it from the completion; tag outcomes with a
   generation and drop stale ones. Do NOT submit every edit - the FIFO queue would replay stale work.
3. **The work closure must be self-contained** - it may touch only the request data it captured and its
   own slot. No UI objects, no databases, no page members. Anything registry-like it needs (e.g.
   `DistanceFieldFonts::Initialize()`) must be initialized on the main thread BEFORE submitting.
4. **Headless fallback**: `EditorContext::Jobs()` can be null (tests construct pages without an app) -
   run the work synchronously then.

## Reference implementation

`FontEditorPage` (`Code/Editor/Editor.Fonts/`): `CaptureBakeRequest()` snapshots the asset options on
the UI thread, `RunBake(request, outcome)` is the pure worker, `StartBake` submits with a `BakeSlot`
lifetime guard, and `ApplyBakeOutcome` applies on the main thread with a generation check. The old
preview stays on screen until its replacement lands.
