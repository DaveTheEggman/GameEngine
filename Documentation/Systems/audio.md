# Draconic Audio — miniaudio-backed subsystem (design)

Status: **P1 SHIPPED + user-verified on speakers 2026-07-18** (agent-built on a worktree,
merged 8b8b334: vendor 002a447 / core ccef6ca / pipeline 4823e0c / subsystem f8ad66d /
registration+sample d59d39a; post-merge smoke fixes 9a1bfea pad voicing + 581eccf mix +
047d12e component dedupe opt-out - see memory/audio-subsystem for the gotchas).
AudioPlayground demonstrates: ambient bed, 4 pitched 3D emitters (attenuation/pan/
doppler), one-shots, scene-pause fade, bus sliders. **P2 SHIPPED 2026-07-18** (e4cafcd audition page w/ waveform + play-through-runtime-
engine; a46d623 distance low-pass; 035766b music cross-fade; c2f6e38 BusLayout-as-data:
per-bus volume/mute/effect chains (lpf/hpf/delay) cooked + defaultBusLayoutId manifest
v5 applied by player AND Game tab - DEVIATION: fixed four-bus topology kept, the
AudioBus enum is the addressing model and named trees are their own migration;
6ba5f5b AudioUserSettings section persisted <userdata>/<project>.user.settings.xml +
Wren Audio facade (bus volumes/mutes/stopMusic - clip-referencing calls parked with
entity handles)). **P3 SHIPPED 2026-07-19** (15c5df8 SoundCue model+resource, c9f827d asset+builder+
SoundCuePage w/ real-resolution audition, 304b762 cue-on-AudioSource + cue one-shots +
inspector picker, 5351e2c Freeverb (pure :reverb partition) as bus effect kind + listener-
driven reverb ZONES on the scene's Effects child group, 656d63a multi-listener (closest-
pick, up to 4) + playground cue/zone demos). **PARKED LIST CLEARED (agent, merged 2026-07-19)**: named custom bus trees ADDITIVE
over the fixed four (name+parent slots on the layout asset, cycle-rejecting cook, live
reconcile-by-name on apply - voices survive; busName addressing on params/components/
Wren strings; custom-bus voices pause individually with their scene), faded steal
(30 ms decay on a bounded dying arena - ma_sound isn't movable, storage never moves;
DyingVoiceCount observability), true voice-cursor playhead (VoiceStatus.cursorSeconds;
both editor pages use it), Wren playback BY CONTENT PATH (AudioScriptBinding{engine,
subsystem,resources}; playOneShot/playOneShot3D/playCue/playMusic; warn-once no-op on
bad paths; script-LEVEL Wren tests), and per-voice reverb sends (splitter -> a wet-only
second per-scene Freeverb; AudioReverbParams.dry disambiguates insert vs aux-send).
ONLY Traktor grain banks remain (north star).

## 9. Grain banks — DEFERRED with a growth path (decided with the user 2026-07-19)

Traktor's grains = a compositional sound-graph (sequence/random/repeat/simultaneous/
envelope/blend/in-loop-out nodes) - FMOD-event-lite as data, leaning on a GAME-PARAMETER
system we don't have. Decision: do NOT port it big-bang. Rationale: (1) the authoring
model and especially the tree-editor UI would be designed in a vacuum until a real
game/demo pulls on specific behaviors; (2) audio is already the most complete
subsystem - the engine's highest-leverage open item is Wren entity handles, and
script-driven gameplay is exactly what will surface real grain requirements; (3) the
SoundCue model grows into grains INCREMENTALLY, each step small and independently
useful:
  a. **In-loop-out** on cues (intro clip -> sustain loop -> tail on stop) - the
     most-wanted behavior; the first bite when audio work resumes.
  b. **Parameter system** (`Audio.setParameter("rpm", v)`) - the genuinely new
     primitive everything else hangs off.
  c. **Blend cues** (crossfade variants by a parameter - engine RPM layers).
  d. **Sequence/composite cues** (cues nesting cues) - at which point we effectively
     HAVE grains, grown rather than ported.
Backend: **miniaudio** (roadmap-locked). Single-file, no external deps, backends for
WASAPI/ALSA/PulseAudio/CoreAudio/AAudio/**WebAudio (emscripten)** — covers the desktop-now,
web/Android-later platform plan with one backend library.

## 1. Goals

- Clips and streams as first-class **assets** (import options, cooked through the pipeline,
  pak-friendly), playing through a **bus mixer** that is itself data.
- 3D spatialization that actually works end-to-end (Sedulous shipped attenuation+pan only;
  its pitch/doppler/low-pass/bus-effects were API without implementation).
- ECS integration in our value-pool component style with the standard two-phase
  `resource::Ref` story and per-frame transform sync.
- Lean but not dead-ended: fire-and-forget one-shots + persistent sources now; buses/effects
  now-ish; grain/random containers later.

## 2. Reference survey — conclusions

**Sedulous** (`Sedulous.Audio*`): pull-graph mixer + bus tree, SDL3 device sink. Cautionary
findings: pitch stored but never resampled (non-48k clips play at the wrong speed!), doppler
and distance-LPF fields never wired, bus effects never invocable from data, streaming =
WAV-only and **bypasses the whole graph** (music gets no buses), cooked audio = uncompressed
PCM sidecars, no voice cap, cue instance accounting leaks. Keep: the generation-checked
playback handle; the buffered contact—er, the threaded command-queue idea (see Traktor).

**Godot** (`servers/audio`): the gold standard for buses — a **send-graph bus layout as a
serialized resource** with per-bus effect chains; atomic voice state machine that always
**fades on pause/stop/delete** (no clicks) and never allocates/frees on the audio thread;
per-voice distance low-pass + reverb-area sends; `AudioStreamPolyphonic` fire-and-forget with
returned IDs; import knobs (`force/mono`, resample cap, loop mode+points, trim, normalize,
compress mode incl. QOA compressed-in-memory).

**Traktor** (`code/Sound`): the richest asset design — serializable resource vs runtime
buffer split, with the **decoder type recorded in the asset** so static clips stay
*compressed in memory* and decode on play; import flags {stream, preload, compressed, gain,
category}; silence-trim on transcode; **category assets with inherited gain/range** (mixer
data without a bus tree); `SoundPlayer` **priority stealing** (free → lower priority →
farther-same-priority) + recent-play dedupe; per-channel command FIFO + double-buffered
filter state (the cleanest game→audio marshalling of the three); up to 4 listeners.

**Lumix** (`src/audio`): the minimal ECS template — module `update()` pushes listener +
source positions and reaps finished voices; runtime mono-guard for 3D sounds. Its own mixer
is a stub on Linux — an argument FOR delegating the mixer to miniaudio entirely.

## 3. Architecture

```
draconic.audio            engine wrapper over miniaudio: AudioEngine, buses, voices,
                          spatializer config, VFS bridge (ma_vfs -> draconic.vfs), Null mode
draconic.audio.resource   AudioClip resource (cooked) + factory; BusLayout resource
draconic.audio.editor     AudioClipAsset + builder + file importer (wav/ogg/mp3/flac)
draconic.audio.subsystem  AudioSubsystem + components (scene integration)
```

- `draconic.audio` wraps miniaudio directly — no `IAudioSystem` abstraction theater
  (Sedulous had a clean interface and still hard-wired the backend at the engine seam).
  What we DO keep abstract: a **Null/headless mode** (engine constructed without a device;
  all calls no-op but handles stay valid) so tests, the cooker, and CI never touch audio
  hardware. miniaudio itself supports the `null` device backend — use it.
- miniaudio building blocks used: `ma_engine` (device + node graph + resource manager),
  `ma_sound_group` per bus, `ma_sound` per voice, `ma_spatializer`(built into ma_sound:
  attenuation models, cone, doppler, min/max distance), `MA_SOUND_FLAG_STREAM` for streamed
  clips, `ma_vfs` hooks bridged to `draconic.vfs` so **streaming works out of paks**.

### 3.1 Mixer / buses

- **BusLayout** = data: named buses, parent (send target), volume, mute, and (phase 2) an
  effect chain per bus. Default layout: Master ← {SFX, Music, UI}. Runtime maps each bus to
  a `ma_sound_group` parented per the layout; volume/mute apply to the group.
- Music is a **streamed clip routed through the graph like everything else** — fixes the
  Sedulous stream-bypass. `PlayMusic` is just a helper that plays a streaming clip on the
  Music bus with cross-fade (phase 2).
- Per-bus effects (phase 2): miniaudio node graph — start with the nodes miniaudio ships
  (`ma_lpf/hpf/bpf/notch/peak/loshelf/hishelf/delay`) + a reverb node (port a Schroeder or
  use miniaudio's `ma_reverb` extra); effect chains serialize in the BusLayout.

### 3.2 Voices

- **Handle**: `{slot, generation}` (Sedulous pattern, kept) over a FIXED voice pool
  (configurable, default 64 + 8 streams). No allocation after init.
- **Stealing** (Traktor policy): on pool exhaustion — free slot → lowest priority below the
  new sound → farthest same-priority. Plus recent-play dedupe (same clip within ~1/30 s
  merges) to stop shotgun-pellet stacking.
- **Stop/pause always fades** (~10 ms, Godot rule) — miniaudio `ma_sound_set_fade_*` does
  this natively; voices reap on the game thread after the fade completes.
- All engine API calls happen on the **main thread**; miniaudio owns the device/mix thread
  internally and its control API is thread-safe for this pattern. No home-grown DSP thread.

### 3.3 3D

- One active listener (component-selected); multi-listener deferred (Traktor supports 4 for
  split-screen — our split-screen story can revisit).
- Per-source: attenuation model (inverse/linear/exponential — miniaudio native), min/max
  distance, cone (inner/outer angle + outer gain), doppler factor (miniaudio computes from
  velocities — we feed per-frame velocity from transform deltas), pan/spatialization toggle.
- **Distance low-pass** (the muffling-with-distance both Godot and Traktor implement and
  Sedulous left dead): an `ma_lpf` node per 3D voice, cutoff driven by distance each sync.
  Phase 2; the node slot is reserved in the voice chain from day one.
- **Mono-for-3D enforced at import** (default force-mono when the clip is marked 3D-intended;
  Godot) AND guarded at runtime (spatializing a stereo clip logs once and downmixes; Lumix).

## 4. Runtime resources

- **`AudioClip`** (cooked product, `draconic.audio.resource`): metadata (channels, rate,
  duration, loop points, gain, `stream` flag, `keepCompressed` flag) + the **original
  compressed container bytes** as the data stream (Traktor: record the decoder; miniaudio
  decodes wav/flac/mp3/vorbis natively). No PCM sidecar bloat. `stream=true` clips are
  decoded on the fly from the (pak-backed) VFS via the `ma_vfs` bridge; in-memory clips
  either decode-on-load (`keepCompressed=false`) or decode-on-play (`ma_decoder` from
  memory) for large-but-latency-tolerant sounds.
- **`BusLayoutResource`**: the serialized bus tree (+ effect chains, phase 2). Project
  settings reference one (`defaultBusLayoutId`); absent = built-in Master/SFX/Music/UI.
- Factories: `AudioClipFactory` (model-A style: builds the runtime clip object, no GPU
  analog needed), `BusLayoutFactory`. Components hold `resource::Ref<AudioClip>` with the
  standard two-phase bind → `ResolveSceneResources` pass.
- Player/export: clips are ordinary cooked products in the pak; streaming reads range
  requests through `PakFileSystem` (already supports seek/read).

## 5. Editor-side assets

- **`AudioClipAsset`** (source, `draconic.audio.editor`): references the copied source file
  (like TextureAsset) + import settings. Builder = transcode-free write-through v1
  (validate + probe metadata + apply trim/normalize when enabled + write container bytes),
  keeping the cook cheap; optional re-encode (vorbis) is a later builder option.
- **`AudioFileImporter`** (`IFileImporter`, extensions wav/ogg/mp3/flac) with
  **`AudioImportOptions`** through the new import dialog:
  - `Stream` (default: on for >10 s or >2 MB, computed at import, shown pre-checked)
  - `Force mono` (default on when "3D" intent toggled)
  - `Loop` + loop points (detected from WAV smpl chunk when present)
  - `Trim trailing silence` (Traktor trick), `Normalize`
- **Inspector/preview**: waveform thumbnail (peak render — cheap, Godot/Sedulous both do
  it); an `AudioClipPage` with Play/Pause/Stop audition (the editor runtime owns a real
  AudioEngine; the Null mode is only for headless).
- **Bus layout editing**: v1 = the BusLayout asset edited via the reflection inspector
  (tree of buses); a dedicated mixer panel with meters is explicitly later.
- **Component inspectors**: AudioSource fields via reflection attributes (range sliders for
  volume/pitch, visibleWhen spatial for 3D-only rows); a min/max-distance sphere gizmo via
  the existing component-gizmo registry.

## 6. Scene integration (`draconic.audio.subsystem`)

- **`AudioSourceComponent`** (serializable, value-pool): `clip` (Ref), `bus` (name),
  `volume`, `pitch`, `loop`, `spatial`, `autoPlay`, `priority`, `minDistance`,
  `maxDistance`, `attenuationModel`, cone fields, `dopplerFactor`. Runtime-only: voice
  handle, dirty flags. Play/Stop/Pause via manager API (guid- or handle-addressed) — the
  component has a real runtime control surface (Sedulous gap: no teleport-, er, no
  velocity/impulse-style controls; here: `Play/Stop/Pause/SetPaused` + one-shot helpers).
- **`AudioListenerComponent`**: `isActive`; first active wins; falls back to the active
  camera's transform when no listener exists (Godot behavior — sensible default).
- **Manager tick** (`ScenePhase::PostTransform`): resolve dirty refs → create/refresh
  voices; sync position + forward + velocity (from previous-frame position) to miniaudio;
  autoplay on scene simulation start; reap finished one-shots. Scene simulation pause pauses
  the scene's voices (miniaudio group pause on a per-scene group under the bus).
- **Subsystem API** (engine-global): `PlayOneShot(clip, bus)` /
  `PlayOneShot3D(clip, position, params)` → handle; `Stop(handle)`, `IsPlaying(handle)`;
  music helpers; master/bus volume accessors (persisted via `draconic.settings` user file —
  volume sliders come free with the settings system).
- **Scripting (Wren)**: `Audio.playOneShot(clipRef, ...)`, handle methods, bus volume — via
  the reflection registration path.

## 7. Phasing

- **P1 — core + clips**: `draconic.audio` engine wrapper (device, groups for the default
  4-bus layout, voice pool + handles + stealing + fades, VFS bridge, Null mode),
  AudioClip resource+factory, AudioClipAsset+builder+importer(+options dialog entries),
  components + manager + one-shots, mono-guard, sample proof (Sandbox: ambient loop +
  positional one-shots) + player. Tests: Null-mode engine (handle/steal/fade state
  machine is pure logic), clip cook round-trip, component serialization.
- **P2 — mixer data + polish**: BusLayout asset + resource + inspector editing, per-bus
  effect chains, distance low-pass, music cross-fade, waveform thumbnails + audition page,
  settings-persisted volumes, Wren exposure.
- **P3 — containers + advanced**: RandomContainer/SoundCue-style asset (weighted variants,
  pitch/volume randomization — Traktor's grain banks as the long-term north star),
  reverb zones (Area-based sends, Godot pattern), multi-listener if split-screen lands.

## 8. Open questions

1. Per-scene bus sub-tree (each scene gets a child group under Master for clean
   pause/teardown) vs global buses only? Recommendation: per-scene group under the
   layout's buses — pause/stop-all per scene falls out naturally (play-in-editor needs it).
2. Compressed-in-memory default for SFX (`keepCompressed`) — decode-on-play costs CPU per
   voice; decode-on-load costs memory. Recommendation: decode-on-load for SFX (typical
   sizes are small), compressed only when flagged.
3. Do we re-encode to vorbis at cook for oversized WAV sources? Defer; write-through keeps
   the pipeline honest first.
