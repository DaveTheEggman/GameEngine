# Build times (future track)

> ARCHIVED 2026-09-01: still-open items live in Documentation/Plans/week-2026-09-05.md
> ("Backlog folder absorbed"). This archive keeps the full detail.
> CORRECTION at archive: the ccache/sccache lever below is DEAD - ccache is banned with C++20 modules (proven stale-BMI corruption, 2026-08-30). Superseded by build-times-profile-history.md (the measurements this doc asked for).


> Raised by the user 2026-08-17: builds are too slow. Task #136. Nothing
> committed to a plan yet - MEASURE FIRST, then pick targets.

## Step one: measurement, not guesses

Where does the wall-clock actually go? Deliverable: a short profile doc.
- `.ninja_log` analysis (slowest edges, critical path) for a clean and an
  incremental build of `Tools.Editor` on clang and gcc.
- Peak memory per TU (the `-j` cap of 4 exists because full parallelism
  OOM-killed a build - memory pressure IS part of the cost).
- Interface-edit fan-out: which module interface edits cascade widest. The
  primary-module `export import :partition` pattern means ANY partition edit
  rebuilds every importer of the whole module - measure the worst offenders
  (foundation.ui? foundation.core?).

## Candidate levers (rank AFTER measuring)

- Interface hygiene: the GCC gcm-cluster rule exists (heavy third-party
  headers + reflect bodies in impl units) but may have regressed as code
  grew; sweep the fattest interfaces.
- Module granularity: some libraries are one huge module - splitting hot
  ones reduces the edit blast radius.
- Linker: static archives + bfd/lld - try mold; measure link share.
- Debug info: `-gsplit-dwarf` (smaller objects, faster links).
- Caching: ccache/sccache C++20-modules support has been maturing - test
  whether PCM/GCM-aware caching works for our setup.
- ThirdParty: confirm vendored libs never rebuild in incremental flows.
- Memory: if fat TUs are the OOM source, splitting them may raise the
  usable -j more than any per-TU speedup.
