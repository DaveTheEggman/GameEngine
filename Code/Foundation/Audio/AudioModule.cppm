// Foundation::Audio - the `foundation.audio` module.
//
// The engine wrapper over the vendored miniaudio: AudioEngine
// (device + node graph + resource manager), the default Master<-{Effects,Music,UI} bus
// groups, the fixed voice pool with generation-checked handles, priority stealing +
// recent-play dedupe, always-fade stop/pause, the ma_vfs -> foundation.vfs stream bridge,
// and a headless Null mode. miniaudio is the committed backend with NO abstraction
// layer, but ma_* types never cross the public surface. Cooked clip records + the
// resource factory live in foundation.audio.resource; scene integration lives in
// engine.audio.

export module foundation.audio;

export import :clip;
export import :cue;
export import :engine;
export import :reverb;
