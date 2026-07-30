# Tint (Dawn WGSL tooling) - vendored, VERSION PROVISIONAL

Tint is used as the **cook-time WGSL conformance validator** (Chrome/Dawn is the strict
frontend; see docs/design/shaders.md P3). naga-cli is the translator; tint is the oracle we
run over naga's WGSL output to fail the web cook on anything Chrome would reject.

## Current binaries (PROVISIONAL)

These were copied from a local FlaxEngine checkout to unblock the P3 spike. They are
**stripped and carry no version** (`tint --version` is unsupported on this build):

- `bin/linux-x86_64/tint`  - ELF x86-64, BuildID sha1 42477a9cbbe766cf2455a0ea238bee180b936bcb
- `bin/mac-arm64/tint`     - Mach-O arm64

Source: `FlaxEngine/Source/Platforms/Web/Binaries/Tools/{Linux/x64,Mac/ARM64}/tint`.

## TODO: replace with a pinned build

Build tint from Dawn at a known revision and record it here, so the conformance oracle has a
reproducible version invariant (as wgpu-native v29.0.1.1 / naga 29.0.x is for the translator).
Dawn ships a standalone CMake build; the `tint_cmd` target is the executable. No Google
prebuilt tint distribution exists - from source is the only authoritative path.

The uniformity rule tint enforces (the one that surfaced forward.ps / taa.ps in the spike) is
WGSL-spec-stable across tint versions, so even this provisional binary is a valid oracle for
that class of error; a pinned build is about reproducibility, not correctness of this finding.
