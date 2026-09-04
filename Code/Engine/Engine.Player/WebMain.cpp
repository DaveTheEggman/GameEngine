// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// WebMain.cpp - the BROWSER entry point for Engine.Player. Shares PlayerApplication.h with the
// desktop Main.cpp and runs the exact same generic game runner; it differs only in the platform
// trio (web shell + WebGPU + the requestAnimationFrame runner, via APP_MAIN's web body)
// and in how the game reaches it: the browser has no argv, so the player FETCHES the dist from
// the SERVING FOLDER at startup - player.xml + Content.pak (the export output) and shaders.dpak
// (the export-cooked WGSL engine pack; browsers have no shader compiler) - into the MEMFS root,
// then runs with projectDir ".". Nothing is baked at link time, which is what makes this binary
// a reusable EXPORT TEMPLATE: export any project, drop the files next to the player, serve the
// folder, browse. The fetches are synchronous under ASYNCIFY (the same yield mechanism the GPU
// waits use).

#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"
#include <emscripten/emscripten.h>

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.fonts;
import foundation.fonts.resource;
import foundation.shell;
import foundation.shell.web; // WebShell (the browser shell) - required by APP_MAIN's web body
import foundation.graphics;
import foundation.graphics.gpu;
import foundation.rhi;        // adapter probe: pick the content pak by compressed-family (P3b)
import foundation.rhi.webgpu; // the browser's WebGPU backend (for the pre-boot adapter probe)
import foundation.runtime;
import foundation.runtime.client;
import foundation.runtime.web; // RunApplication (the rAF runner) - required by APP_MAIN
import engine.defaultapp;
import foundation.scene;
import engine.scene;
import foundation.scene.resource;
import foundation.render;
import engine.render;
import foundation.animation;
import foundation.animation.resource;
import engine.animation;
import foundation.particles;
import foundation.particles.resource;
import engine.particles;
import foundation.geometry;
import foundation.geometry.resource;
import foundation.audio;
import foundation.audio.resource;
import engine.audio;
import foundation.materials;
import foundation.materials.resource;
import foundation.texture;
import foundation.texture.resource;
import foundation.image.resource;
import foundation.model.resource;
import foundation.script;
import foundation.script.resource;
import foundation.input;
import foundation.physics;
import foundation.physics.resource;
import engine.physics;
import foundation.input.resource;
import engine.input;
import foundation.ui.resource;
import foundation.ui;           // View / ViewGroup / ProgressBar (the boot-splash controls)
import engine.ui;
import engine.gameinstance; // SceneLoadHandle (PlayerApplication async level load)
import foundation.xml.serialization;
import foundation.settings;
import engine.project;
import foundation.vfs.pak;

#include "PlayerApplication.h"      // the shared runner (uses the imports above)
#include "Runtime.Client/AppMain.h" // APP_MAIN (web body: WebShell + WebGPU + rAF runner)

using namespace foundation::core;

namespace
{
    // Pull one dist file from the serving folder into the MEMFS root. Synchronous under
    // ASYNCIFY (emscripten_wget yields to the browser while the request runs). A build
    // that PRELOADED the file (e.g. the WebScene sample shape) skips the fetch.
    void FetchDistFile(const char* name)
    {
        const StringView path(reinterpret_cast<const utf8char*>(name));
        if (FileExists(path))
        {
            return; // preloaded/bundled - nothing to fetch
        }
        emscripten_wget(name, name);
        if (!FileExists(path))
        {
            LOG_ERROR(u8"Player", u8"could not fetch '{}' from the serving folder - "
                                           u8"is it next to the player page?",
                               path);
        }
    }

    // Fetch `src` from the serving folder, saving it to MEMFS under `dst`. Synchronous under ASYNCIFY.
    // Returns whether the destination file exists afterwards (a 404 leaves it absent).
    bool FetchDistFileAs(const char* src, const char* dst)
    {
        emscripten_wget(src, dst);
        return FileExists(StringView(reinterpret_cast<const utf8char*>(dst)));
    }

    // Pick the content variant pak by the browser's compressed-texture family and fetch it AS
    // Content.pak, so the project loader (which reads Content.pak) needs no change (asset-variants
    // P3b, Fable ruling Q1). A web dist built by this engine ships Content-bc.pak (desktop browsers)
    // + Content-astc.pak (mobile browsers); a single-pak/old bundle falls back to Content.pak.
    //
    // The probe creates a throwaway WebGPU backend to read the adapter's textureCompressionBC/ASTC
    // flags (Asyncify makes requestAdapter synchronous here, the same yield the fetches use), then
    // tears it down before the app boots and creates its own device (two adapter requests/page is
    // fine). If neither family is reported (spec-impossible for a WebGPU device), we log and fall
    // back to the single pak rather than render nothing.
    void SelectAndFetchContentPak()
    {
        if (FileExists(u8"Content.pak"))
        {
            return; // preloaded/bundled single pak - nothing to select
        }

        bool bc = false;
        bool astc = false;
        foundation::rhi::Backend* probe = nullptr;
        if (foundation::rhi::webgpu::CreateBackend(foundation::rhi::webgpu::WebGpuBackendDesc{},
                                                   probe, DefaultAllocator())
                .IsOk() &&
            probe != nullptr)
        {
            const Span<foundation::rhi::Adapter* const> adapters = probe->EnumerateAdapters();
            if (!adapters.IsEmpty())
            {
                const foundation::rhi::AdapterInfo info = adapters[0]->Info();
                bc = info.supportedFeatures.textureCompressionBC;
                astc = info.supportedFeatures.textureCompressionASTC;
            }
            probe->Destroy();
        }

        // Prefer BC (desktop browsers); ASTC is the mobile family. A device won't usually have both.
        const char* variant = bc ? "Content-bc.pak" : (astc ? "Content-astc.pak" : nullptr);
        LOG_INFO(u8"Player", u8"content-variant probe: bc={} astc={} -> {}", bc, astc,
                 StringView(reinterpret_cast<const utf8char*>(variant ? variant : "Content.pak")));

        if (variant != nullptr && FetchDistFileAs(variant, "Content.pak"))
        {
            return; // the matching variant pak is now mounted as Content.pak
        }
        // No variant pak on the server (single-pak / old bundle), or neither family: the single pak.
        FetchDistFile("Content.pak");
    }

    // Default-constructible so APP_MAIN can own it in static storage: the dist is
    // fetched from the serving folder into the MEMFS root, so the project dir is ".".
    class WebPlayerApplication final : public engine::player::PlayerApplication
    {
    public:
        WebPlayerApplication() : PlayerApplication(MakeOptions()) {}

    private:
        static engine::player::PlayerOptions MakeOptions()
        {
            // Fetch BEFORE the app boots: the project loader reads player.xml/Content.pak
            // during Initialize, and the render subsystem loads shaders.dpak on device init.
            FetchDistFile("player.xml");
            // Content: pick the variant pak by the browser's compressed-texture family (BC vs ASTC)
            // and mount it AS Content.pak, so the loader is unchanged (asset-variants P3b).
            SelectAndFetchContentPak();
            FetchDistFile("shaders.dpak");
            engine::player::PlayerOptions options;
            options.projectDir = String(u8".");
            return options;
        }
    };
}

APP_MAIN(WebPlayerApplication)
