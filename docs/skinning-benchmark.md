# Skinning Benchmark & Baseline

`Code/Samples/AnimStressTest` is the skeletal-animation throughput benchmark — a port of
Sedulous's `EngineAnimationSandbox`. It spawns a grid of the Quaternius `Char` humanoid; every
instance gets its own `AnimationPlayer` (random clip + small speed jitter + desynced start) while
sharing one cooked model (mesh / skeleton / clips / materials).

It exists to measure the GPU-skinning architecture so we can compare **before vs after** the
Flax/Sedulous persistent-shared-bone-buffer + instanced-skinning rewrite.

## Running

```
# Debug (validation ON — correctness, not throughput)
cmake --build build/clang --target AnimStressTest
./build/clang/Code/Samples/AnimStressTest/AnimStressTest

# Release (RelWithDebInfo, validation OFF — true perf)
cmake --build build/relwithdebinfo --target AnimStressTest
./build/relwithdebinfo/Code/Samples/AnimStressTest/AnimStressTest
```

Controls: **Space** = +25 characters, **Backspace** = −25 (camera auto-reframes to keep the whole
grid in view), **WASD/QE + RMB** fly, **P** = profiler dump (CPU scope tree incl. `Anim.Drive`, plus
GPU pass timings), **Esc** = exit. vsync is OFF (`PresentMode::Immediate`) so the frame time is real
CPU+GPU work, not the display refresh.

`kAutoRamp` (a `static constexpr bool` in `main.cpp`, **false** by default) is a measurement aid: flip
it to `true` for a headless run that ramps the character count until FPS ≤ 50, prints the threshold,
and exits. The backend prints `[Vulkan] validation layers: ENABLED/DISABLED` at startup so a perf run
can confirm validation is off.

## BEFORE baseline — current non-instanced skinning (captured 2026-06-29)

| Build | Validation | Result |
|-------|-----------|--------|
| **RelWithDebInfo** | off | **~1,683 fully-rendered animated characters @ 50 fps (≈20 ms)** |
| Debug | on | ~100 chars @ 31 fps (validation-dominated — not a throughput measure) |

**~1,683 chars @ 50 fps is the number the rewrite must beat.**

### Frame breakdown @ 1,000 chars (RelWithDebInfo, validation off) — 86 fps / 11.66 ms

```
Update:            4.61 ms   (CPU)
  Anim.Drive:      4.32 ms     per-instance player eval (sample clip + compute skinning matrices)
Render:            7.15 ms   (CPU)
  Render.Extract:  0.35 ms
  Render.Compose:  6.46 ms
    Compose.Execute: 6.26 ms   record + submit + wait on GPU

GPU passes (total 4.44 ms):
  shadow.cascade ×4   ~3.16 ms   (CSM — the dominant GPU cost)
  forward             1.03 ms
  shadow.atlas ×2     ~0.14 ms
  cluster.build       0.08 ms
  tonemap             0.02 ms
```

Read: the frame is **CPU-bound** (Update 4.6 + Render 7.1 ≈ 11.7 ms serial, while the GPU finishes in
4.4 ms). The two CPU costs the rewrite targets are `Anim.Drive` (per-player skinning matrix
computation, redone every frame) and the per-pass bone uploads inside `Compose.Execute`. GPU-side, a
persistent shared bone buffer + instanced skinned draws should cut the forward + cascade draw counts.

### Frame breakdown @ 1,000 chars (Debug, validation ON) — 5 fps / 221 ms

```
Update:           24.3 ms   (Anim.Drive 23.8 ms)
Render:          238.0 ms   (Compose.Execute 235.8 ms  <- Vulkan validation-layer CPU overhead)
GPU passes:       23.0 ms   (forward 6.1, cascades ~12.9, cluster 3.2)
```

Debug is **validation-bound**, not skinning-bound: `Compose.Execute` (command record + submit) is
~236 ms because the validation layers inspect every command — ~95% of the frame, vs 6.3 ms in release.
Develop/correctness-check in Debug, but treat **release** as the perf baseline. (Both `Anim.Drive` and
the GPU pass times are ~5× the release figures, as expected for an unoptimized build.)

## Limitations this benchmark exposed (what the rewrite should fix)

1. **Bones re-uploaded per pass.** Each caster's bone matrices are uploaded separately for the forward
   pass *and* every CSM cascade (and local-shadow passes) — roughly `N × bones × (5+)` matrices per
   frame. This overflowed the old 262,144-slot per-frame ring at ~1,800 chars, silently dropping the
   *forward* draws (characters vanished from the main view while their already-allocated shadow
   casters kept rendering in the light pools). Worked around for the baseline by bumping
   `kMaxBoneMatrices` to `1<<20` in `MeshRenderer.cppm`. The rewrite should upload each character's
   bones **once** into a persistent buffer shared across all passes.
2. **Skinned meshes can't be instanced.** The instanced draw path had no per-instance bone matrices,
   so identical skinned meshes collapsed to a single bind pose. Fixed defensively (skinned draws are
   forced to the single-draw path in both forward and shadow batching). Real instanced skinning is the
   rewrite's job.
3. **Full skeleton spawned as scene entities.** Each character spawns its entire node hierarchy
   (~34 entities including skeleton joints) → 13,600 entities at 400 chars, inflating the scene
   transform-update cost. Sedulous uses ~1 entity per character (the skeleton lives inside the player).
