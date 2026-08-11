# Deferred by design (do NOT build without a user go-ahead)

These are known, consciously parked. Listed so the build agent does not
"helpfully" start one. Each has context in memory/design docs; ask the user
before touching any.

- **Skinned instanced crowds**: SHIPPED 2026-08 (InstancedSkinning O(M) shared
  pose pool, 2b360ab4; + per-clip bucketing, caller-selectable pose assignment,
  runtime part-merge, AnimatedCrowd HUD). No longer deferred. (docs/instanced-crowds.md)
- **GPU particle simulation**: CPU model shipped; GPU-compute sim is the
  planned phase 2 (docs/design particles plan).
- **Post-processing phases 4/5**: post stack phases 1-3 shipped and PAUSED
  pending the user's on-screen verify; 4/5 (remaining effects) not started.
- **AngelScript debugger P2/P3**: in-process debugger + editor story shipped;
  profiler (P2) and remote transport (P3) queued behind demand.
- **draconic.gui (Experimental)**: parked; only the keyframes test fix
  (gui-tests-keyframes.md) is in scope.
- **SIMD math layout migration**: measured 1.05x for the naive swap -
  worthless; the real win needs cross-op SIMD data layout. Parked until a
  profiled consumer justifies it.
- **Web pthreads / SharedArrayBuffer**: hosting-header constraints; single
  threaded web stays the contract until revisited.
- **MemCompare dirty-signal on the WebGPU persistent-map flush**: known ~1ms
  headroom cost, deliberately left (idle-bound); revisit only if a scene goes
  CPU-bound on web.
