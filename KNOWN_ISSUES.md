# Known issues

Tracked defects we've triaged but not yet fixed. Each entry: what, where, impact,
and the plan. Keep newest first.

---

## ctest: intermittent `DraconicEditorCoreTests` failure under parallel load — FIXED

**Status:** FIXED 2026-07-20. Was the recurring "1 tests failed on the first run after a
build" flake seen many times across clang and gcc this session.

**Root cause (a real bug, not just a flaky test):** `EditorJobService::Update`
(`JobService.cppm`) drained a job's log, then on completion called `m_ctx.Reset()`
**without a final drain**. A log line the worker appended *between* that drain and the
reset was discarded — so a fast job could silently drop its last log lines. The
`jobs: ... sawLog` assertion (`JobServiceTests.cpp:54`) caught it: under peak parallel
load the "halfway" log landed in that window and was lost.

**Fix:** `Update` now does a final drain of `m_ctx->m_log` **after `JoinWorker()`** (worker
joined ⇒ no more appends) and before `m_ctx.Reset()`, so completion never drops a job's
last logs. Verified: two full gcc ctest runs 87/87 after the fix (previously failed nearly
every first run under load).

---

## AngelScript coroutine scheduler: `$func` GC warning at engine shutdown

**Status:** open — cosmetic, low priority (shutdown-only console warning, no functional leak).

**Symptom:** the AngelScript coroutine scheduler prints a `$func` garbage-collector warning
when the engine is destroyed (seen in the AngelScript conformance battery, whose coroutine
section holds coroutine contexts/funcdef handles). The isolated delegate test is
warning-free, so it's specific to the coroutine path.

**Cause:** the coroutine scheduler (own `asIScriptContext` per coroutine + AddRef'd
funcdef/function handles) doesn't release every held handle before engine teardown, so
AngelScript's GC reports a lingering reference at shutdown. Benign (the process is ending)
but it means the scheduler's teardown isn't releasing all coroutine handles.

**Plan:** audit the coroutine scheduler's teardown to release all held `asIScriptFunction*`
/ context handles before the engine is destroyed; the warning should disappear.

---

## AngelScript: misaligned `asPWORD` read in bytecode dispatch (UBSan)

**Status:** open — vendored third-party bug, benign on x86-64, fix upstream + carry a patch.

**Diagnostic** (reproduce: build `build/asan` — `-fsanitize=address,undefined
-fno-sanitize-recover=all` — and run `Bin/Debug/Linux64-Clang-ASAN/DraconicScriptAngelScriptTests`):

```
ThirdParty/angelscript/source/as_scriptfunction.cpp:1222:47: runtime error:
load of misaligned address 0x... for type '::asPWORD' (aka 'unsigned long'),
which requires 8 byte alignment
```

**Where:** `as_scriptfunction.cpp:1222`, the `asBC_ALLOC` case of the bytecode walk:
`asCObjectType *objType = (asCObjectType*)asBC_PTRARG(&bc[n]);`. `asBC_PTRARG`
reads an `asPWORD` (pointer-width) directly out of the packed bytecode array `bc[]`,
whose instruction stream is `asDWORD`-aligned (4 bytes), so a pointer argument can
land on a 4-byte (non-8-byte-aligned) offset. Same pattern recurs for every
`asBC_PTRARG`/`asBC_INTARG` pointer read across the interpreter and serializer.

**Trigger:** any script-class instantiation (`asBC_ALLOC` executes) — it is NOT
specific to our code. First seen on the pre-existing "instantiate a script class"
AngelScript test; the coroutine work did not introduce it.

**Impact:** none in practice on x86-64/ARM64, which permit unaligned loads — this is
strict-UB flagged by UBSan, not a functional fault. It only matters (a) as noise that
masks real UBSan findings in our own AngelScript-linked tests, and (b) on a
hypothetical alignment-strict target. AngelScript is a committed backend
([[scripting-backend-neutrality]]).

**Plan / options:**
1. **Upstream PR** to https://github.com/anjo76/angelscript (the repo we vendored
   2.39.0-WIP from): route pointer args through a `memcpy`-based read in `asBC_PTRARG`
   (the standard fix for packed-stream reads) so the load is alignment-safe. This is the
   correct long-term fix; AngelScript upstream has historically been aware of the packed
   pointer-arg design.
2. **Carry a local vendor patch** in `ThirdParty/angelscript` in the meantime (a
   `DRACONIC_PATCHES/` note + the diff) so our sanitizer builds are clean before upstream
   merges. Requires touching vendored source — do it deliberately, documented here.
3. **Suppress** via a UBSan `alignment` suppressions file scoped to
   `ThirdParty/angelscript/*` so our own code's UBSan signal stays clean without editing
   the vendor drop. Cheapest; hides the issue rather than fixing it.

Recommendation: (3) now to unblock clean sanitizer runs on our code, then (1) upstream.
