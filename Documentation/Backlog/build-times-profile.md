# Build-times profile (task #136) - measurements, 2026-08-24

Measured by Fable per the backlog's "measure first" step (build-times.md). All
numbers from ISOLATED build dirs (`build/timing` clang, `build/timing-gcc` gcc;
`BUILDSYSTEM_OUTPUT_SUFFIX` keeps Bin separate), Debug, Ninja, target
`Tools.Editor`, `-j4` unless stated, on the 16-core / 14 GB dev machine. Per-TU
wall/user/peak-RSS captured via a `/usr/bin/time` compiler launcher; per-edge
times from `.ninja_log` (DEDUPED - clang module TUs list `.o` + `.pcm` as two
entries of one edge, which inflates naive sums ~1.5x); fan-out computed exactly
from the P1689 `.ddi` module graph (ninja dry-runs cannot see through dyndep -
do not use them for this).

## Headline numbers

| scenario (clang, -j4) | wall | TUs rebuilt |
|---|---|---|
| clean Tools.Editor | 228s | 2850 edges (889s CPU, 3.99x saturation) |
| no-op | 0.35s | 0 |
| impl-unit edit (SculptImpl.cpp) | 3.7s | 1 (+archive+link) |
| UI.Toolkit interface edit (Docking.cppm) | 31.6s | 93 |
| foundation.ui interface edit (View.cppm) | 52.7s | 252 |
| foundation.core interface edit (Guid.cppm) | 83.2s | 767 |

GCC clean: 950s compile-CPU vs clang's 889s (~7% more; est. ~240s wall) -
NOT dramatically slower overall, but its hot spots differ (below). One GCC run
died on a transient unexplained subcommand failure (no OOM record, no failed
compile in the per-TU log; resumed clean) - watch for recurrence; suspect a
module-mapper race.

## Finding 1: ThirdParty is HALF the clean build - and jolt is 37% alone

Deduped compile-CPU, clean Tools.Editor:

| target | clang | gcc | TUs |
|---|---|---|---|
| jolt | 327s (37%) | 351s (37%) | 267 |
| all ThirdParty | 405s (46%) | 435s (46%) | ~700 |
| RHI.WebGPU | 47s | 16s | 63 |
| Editor.Scene | 42s | 76s | 65 |
| UI | 40s | 36s | 301 |
| UI.Toolkit | 34s | 24s | 99 |
| luau | 34s | 37s | 127 |
| Editor.App | 28s | 31s | 59 |

Vendored code that never changes costs half of every clean build. Confirmed it
does NOT rebuild in incremental flows (impl edit = 3 edges; core-interface edit
rebuilds 767 TUs, none of them ThirdParty). So this hits clean builds, CI-style
builds, and new build dirs only - but there it is the single biggest lever.

## Finding 2: interface-edit fan-out - the chokepoint edges

Exact module blast radius (TUs recompiled after an interface edit; Tools.Editor
graph = 1247 module TUs):

| module | transitive | direct | carrier of the cascade |
|---|---|---|---|
| foundation.core | 852 (68%) | 796 | (everything imports core - structural) |
| foundation.image | 376 | 46 | fonts stack interfaces import image |
| foundation.vfs | 324 | 46 | content interface imports vfs |
| foundation.fonts | 311 | 55 | ui interface imports fonts |
| foundation.xml | 213 | 6 | **foundation.ui interface imports xml** |
| foundation.messaging | 212 | 3 | **foundation.scene interface imports messaging** |
| foundation.vg.svg | 207 | 2 | **foundation.ui + ui.runtime interfaces import vg.svg** |
| foundation.profiler | 162 | 21 | every engine.* interface imports profiler |
| foundation.render.api | 146 | 7 | engine.render/foundation.render interfaces |
| foundation.materials.pipelinecache | 139 | 4 | render interfaces |

The bolded rows are the pathology: a module with 2-6 direct importers whose
edit rebuilds ~17% of the world, because ONE interface (`export import` /
interface-unit `import`) of a mid-level module carries it upward. The primary-
module `export import :partition` pattern means any partition edit dirties the
whole module, so these single edges set the blast radius for every partition of
the importing module too.

## Finding 3: memory is NOT the -j4 justification for compiles

- clang peak single-TU RSS: 791 MB (InspectorViewImpl.cpp; the Editor.Scene
  pages cluster at 550-790 MB - each one materializes the whole editor module
  set). gcc peak: 446 MB (jolt).
- Whole-build peak (cmake process tree): ~810 MB at -j4.
- At **-j8** the machine holds (free dips to ~1.1 GB + reclaimable cache;
  result below). The historical OOM was presumably the `all` target (tests +
  samples: many concurrent LINKS of debug executables) or a gcc gcm regression
  of that era - Tools.Editor compiles alone do not justify -j4.

**-j8 clean result: 177s wall vs 228s at -j4 (-22%), completed with no OOM,
whole-tree peak RSS ~830 MB.** Sub-linear: total user CPU inflated 785s ->
1188s (+51%) from SMT/cache contention, and the module dependency ladder caps
usable parallelism. So raising -j is SAFE for Tools.Editor and worth ~50s on
clean builds, but is not the big lever; -j6 may be the efficiency sweet spot.

## Finding 4: where a fat TU's time goes (clang -ftime-trace)

InspectorViewImpl.cpp (5.9s under load): Frontend 4.7s, of which template
instantiation 2.3s and parsing ~1s; Backend 1.2s. Module BMI loading is NOT the
cost - re-INSTANTIATING templates per TU is. Splitting fat impl units improves
parallelism but not total CPU; `extern template` on the hottest instantiations
(reflection/property-grid/inspector rows) is the total-CPU lever.

## Finding 5: non-issues (measured, close them)

- **Link share**: 16s of 889s (clang), final Tools.Editor link 4.1s. mold /
  lld / -gsplit-dwarf are not worth pursuing now.
- **Module scan**: 37s over 1247 scans (clean); no-op build 0.35s - scanning
  is not a tax on incremental flows.
- **ThirdParty in incrementals**: never rebuilds (confirmed).
- **GCC vs clang totals**: within ~7% on compile CPU for this target; GCC is
  2x on Editor.Scene (gcm cost on fat editor TUs), clang is 3x on RHI.WebGPU
  (wgpu native headers) - hygiene targets, not compiler-switch arguments.

## Ranked levers (from the measurements)

1. **Cache/prebuild ThirdParty** (halves clean builds): vendored libs never
   change - options: ccache for ThirdParty targets only (object-level, no
   modules involved - vendored code is header-based, so ccache works), or a
   committed/prebuilt archive per compiler+config. Cheapest big win.
2. **Break the three chokepoint edges** (kills the worst incremental
   cascades): move `foundation.messaging` out of foundation.scene's interface
   (impl-unit import or a fwd-declaring partition), `foundation.xml` out of
   foundation.ui's interface, `foundation.vg.svg` out of foundation.ui /
   ui.runtime's interfaces. Three edits, each cuts a ~210-TU blast to ~direct
   size. Then re-measure; image->fonts and vfs->content are the next tier.
3. **Raise -j for compile-heavy flows**: -j8 held for Tools.Editor (177s vs
   228s, no OOM). Adopt -j6/-j8 for compile phases cautiously (or `-j8 -l 12`),
   keep link-heavy `all` batteries at -j4 until test-link memory is measured.
4. **Editor.Scene diet**: 65 TUs each pulling 550-790 MB contexts; the pages
   are independent - splitting the target (or trimming its interface imports)
   improves both parallelism and the 42s/76s (clang/gcc) target cost, and GCC
   doubles it today.
5. **extern template pass** on inspector/property-grid instantiations
   (finding 4) - measure one TU before/after before sweeping.

NOT worth it now (measured): linker swaps, split-dwarf, scan optimization,
compiler switching.

## Repro

- `build/timing-wrap.sh` (in-repo, untracked build/ dir) = the launcher.
- Configure: `cmake -B build/timing -G Ninja -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_CXX_COMPILER=clang++ -DBUILDSYSTEM_OUTPUT_SUFFIX=-Timing
  -DCMAKE_CXX_COMPILER_LAUNCHER=$PWD/build/timing-wrap.sh`
- Fan-out: parse `build/<dir>/**/*.ddi` (P1689) - transitive closure over
  provides/requires. Dry-run `ninja -n` counts are WRONG for this (dyndep).
