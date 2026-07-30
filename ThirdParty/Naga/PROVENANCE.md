# naga-cli - vendored cook-time WGSL translator

naga-cli translates SPIR-V -> WGSL in the **web cook** (see docs/design/shaders.md P3). It is
the version-matched translator for our runtime: we ship **wgpu-native v29.0.1.1**, which bundles
**naga 29.0.x** - the exact WGSL validator our native WebGPU backend links - so naga-cli pinned
to the 29.0 line emits WGSL guaranteed to round-trip through our own runtime.

## Why it is vendored (nobody installs Rust)

This is a **cook-time-only** tool, shelled out to when cooking for the web target - exactly like
DXC (`ThirdParty/DXC`), wgpu-native (`ThirdParty/WgpuNative`), and tint (`ThirdParty/Tint`).

- **Players never touch it.** Dists ship cooked WGSL; the translator does not.
- **Developers never install Rust.** We ship the prebuilt binary here. Rust was used ONCE to
  produce it (`cargo install naga-cli --version 29.0.4`); the resulting standalone executable
  depends only on libc/libm/libgcc_s (no Rust runtime) and is checked in.

## Current binary

- `bin/linux-x86_64/naga` - naga-cli 29.0.4, stripped, ELF x86-64. `VERSION` = 29.0.4.

## TODO: other host platforms

Only the Linux x86-64 cook host is vendored so far. The cook runs on the developer/CI host, so
a `bin/win-x64/naga.exe` and `bin/mac-arm64/naga` are needed for those cook hosts - build each
with `cargo install naga-cli --version 29.0.4` on that platform (or cross-compile) and drop the
binary here. Keep the pin at the 29.0 line to stay matched to wgpu-native v29's naga.
