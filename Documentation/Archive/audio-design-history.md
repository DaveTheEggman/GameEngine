# Audio - design history (archived)

> Status: ARCHIVED
> Superseded by: Documentation/Systems/audio.md
> Track: [[audio-subsystem]]

NON-AUTHORITATIVE. The reference survey and design rationale behind the 2026-07 miniaudio build.
Present-tense truth is `Systems/audio.md`; the full original design doc (goals, phasing P1-P3, the
detailed per-section design) is in git at the P0 commit 3b92560d. Kept for the "why".

## Reference survey - conclusions

**Sedulous** (`Sedulous.Audio*`): pull-graph mixer + bus tree, SDL3 sink. Cautionary: pitch stored
but never resampled (non-48k clips play at the wrong speed), doppler + distance-LPF fields never
wired, bus effects never invocable from data, streaming = WAV-only and bypasses the whole graph
(music gets no buses), cooked audio = uncompressed PCM sidecars, no voice cap, cue instance accounting
leaks. Keep: the generation-checked playback handle; the threaded command-queue idea.

**Godot** (`servers/audio`) - the gold standard for buses: a send-graph bus layout as a serialized
resource with per-bus effect chains; an atomic voice state machine that always fades on
pause/stop/delete and never allocates on the audio thread; per-voice distance low-pass + reverb-area
sends; `AudioStreamPolyphonic` fire-and-forget with returned IDs; import knobs (force/mono, resample
cap, loop mode+points, trim, normalize, compress incl. QOA).

**Traktor** (`code/Sound`) - the richest asset design: serializable resource vs runtime buffer split
with the decoder type recorded in the asset (static clips stay compressed in memory, decode on play);
import flags {stream, preload, compressed, gain, category}; silence-trim on transcode; category assets
with inherited gain/range; `SoundPlayer` priority stealing (free -> lower priority -> farther-same-
priority) + recent-play dedupe; per-channel command FIFO + double-buffered filter state; up to 4
listeners.

**Lumix** (`src/audio`) - the minimal ECS template: `update()` pushes listener + source positions and
reaps finished voices; runtime mono-guard for 3D sounds. Its mixer is a Linux stub - an argument FOR
delegating the mixer to miniaudio entirely.

## Key design calls

- **No abstraction theater.** `foundation.audio` wraps miniaudio directly - Sedulous had a clean
  `IAudioSystem` interface and still hard-wired the backend at the engine seam. What we DO keep
  abstract: a **Null/headless mode** (engine constructed without a device; all calls no-op, handles
  stay valid) so tests, the cooker, and CI never touch hardware. miniaudio's own `null` backend
  provides it.
- **Backend = miniaudio** (roadmap-locked): single-file, no external deps, backends for WASAPI / ALSA
  / PulseAudio / CoreAudio / AAudio / WebAudio (emscripten) - covers desktop-now + web/Android-later
  with one library.
- **Fixed four-bus topology kept** (`AudioBus` = Master/Effects/Music/UI as the addressing model);
  named custom bus trees are their own additive migration on top, not a replacement.
- **Music routed through the graph** like everything else (fixes Sedulous's stream-bypass).
- **Voice handles** `{slot, generation}` over a fixed pool, Traktor stealing policy, Godot always-fade
  rule - no home-grown DSP thread; miniaudio owns the mix thread.

## Open questions (as resolved)

1. Per-scene bus sub-tree vs global buses only -> per-scene child group under the layout's buses, so
   pause/stop-all per scene falls out naturally (play-in-editor needs it). Shipped that way.
2. Compressed-in-memory default for SFX (`keepCompressed`) -> decode-on-load for SFX (small sizes),
   compressed only when flagged.
3. Re-encode oversized WAV to vorbis at cook -> deferred; write-through keeps the pipeline honest
   first.
