# miniaudio (vendored)

- Upstream: https://github.com/mackron/miniaudio
- Version: v0.11.25 (2026-03-04)
- License: public domain OR MIT-0, at your option (license text at the end of `miniaudio.h`).

Vendored BY COPY (single header, no submodule). The `MINIAUDIO_IMPLEMENTATION`
translation unit lives in the engine library that uses it (`draconic.audio`),
mirroring the stb layout. Ogg Vorbis decoding comes from `ThirdParty/stb/stb_vorbis.c`
(public domain), which miniaudio picks up automatically when its header-only part is
included before the miniaudio implementation.
