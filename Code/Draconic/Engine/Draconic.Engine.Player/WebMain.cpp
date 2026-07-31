// WebMain.cpp - the BROWSER entry point for Draconic.Engine.Player. Shares PlayerApplication.h with the
// desktop Main.cpp and runs the exact same generic game runner; it differs only in the platform
// trio (web shell + WebGPU + the requestAnimationFrame runner, via DRACONIC_APP_MAIN's web body)
// and in how the game reaches it: the browser has no argv, so the cooked DIST (Content.pak +
// player.xml) is PRELOADED into the virtual FS at the root by the link step (see CMakeLists), and
// the app runs with projectDir ".". The engine WGSL shader pack rides in the same way at
// /shaders.dpak (browsers have no shader compiler).

#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Log/Log.h"
#include "Draconic.Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.shell;
import draconic.shell.web; // WebShell (the browser shell) - required by DRACONIC_APP_MAIN's web body
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.web; // RunApplication (the rAF runner) - required by DRACONIC_APP_MAIN
import draconic.engine.defaultapp;
import draconic.scene;
import draconic.engine.scene;
import draconic.scene.resource;
import draconic.render;
import draconic.engine.render;
import draconic.animation;
import draconic.animation.resource;
import draconic.engine.animation;
import draconic.particles;
import draconic.particles.resource;
import draconic.engine.particles;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.audio;
import draconic.audio.resource;
import draconic.engine.audio;
import draconic.materials;
import draconic.materials.resource;
import draconic.texture;
import draconic.texture.resource;
import draconic.image.resource;
import draconic.model.resource;
import draconic.script;
import draconic.script.resource;
import draconic.input;
import draconic.physics;
import draconic.physics.resource;
import draconic.engine.physics;
import draconic.input.resource;
import draconic.engine.input;
import draconic.ui.resource;
import draconic.engine.ui;
import draconic.xml.serialization;
import draconic.settings;
import draconic.engine.project;
import draconic.vfs.pak;

#include "PlayerApplication.h"      // the shared runner (uses the imports above)
#include "Draconic.Runtime.Client/AppMain.h" // DRACONIC_APP_MAIN (web body: WebShell + WebGPU + rAF runner)

using namespace draconic::core;

namespace
{
    // Default-constructible so DRACONIC_APP_MAIN can own it in static storage: the dist lives at the
    // preloaded MEMFS root, so there is nothing to parse - the project dir is ".".
    class WebPlayerApplication final : public draconic::player::PlayerApplication
    {
    public:
        WebPlayerApplication() : PlayerApplication(MakeOptions()) {}

    private:
        static draconic::player::PlayerOptions MakeOptions()
        {
            draconic::player::PlayerOptions options;
            options.projectDir = String(u8".");
            return options;
        }
    };
}

DRACONIC_APP_MAIN(WebPlayerApplication)
