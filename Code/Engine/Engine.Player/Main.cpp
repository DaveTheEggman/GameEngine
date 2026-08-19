// Engine.Player - the generic game runner (MVP-to-Export milestone, docs/design/roadmap.md).
//
// Runs a project with ZERO native game code: engine subsystems + the project's content +
// the default scene, simulating - and, when the manifest names one, the project's GAME SCRIPT
// (the scripted IApplication counterpart: a `Game` class in AngelScript OR Luau with
// launch/update(dt)/exit, orchestrating above scenes - the backend resolves by the script's
// language). With scripting, player + scripts + cooked content IS the game; projects that outgrow
// scripts graduate to a native IApplication at the same seam.
//
// This is the DESKTOP entry point (SDL shell + a --backend-selected device + the blocking desktop
// runner + CLI args). The browser sibling is WebMain.cpp; both share PlayerApplication.h.
//
// Usage: Engine.Player <projectDir> [--scene <source-db-path>] [--exit-after <seconds>]
//
// Two modes, detected by layout:
//   PROJECT dir (Project.xml): scenes load from the authored source DB (their cooked form IS
//     the authored form - scenes are builder-less by design), products resolve from Cooked/ -
//     the editor's own runtime path. The dev loop.
//   DIST dir (Content.pak + player.xml, staged by Tools.Export): ONE binary DB inside the pak
//     holds products AND scenes; the game script rides in the pak as a raw entry. Zero editor
//     code links into this binary - the shipping shape.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.fonts;
import foundation.fonts.resource;
import foundation.shell;
import foundation.shell.desktop;
import foundation.graphics;
import foundation.graphics.gpu;
import foundation.runtime;
import foundation.runtime.client;
import foundation.runtime.desktop;
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
import foundation.script.resource; // ScriptClass (the cooked game script, bound from the content DB)
import foundation.input;
import foundation.physics;
import foundation.physics.resource;
import engine.physics;
import foundation.input.resource;
import engine.input;
import foundation.ui.resource;  // UITheme (the manifest's default theme)
import foundation.ui;           // View / ViewGroup / ProgressBar (the boot-splash controls)
import engine.ui; // UISubsystem (IME target + default theme + the boot splash overlay)
import engine.gameinstance; // SceneLoadHandle (the async boot load)
import foundation.xml.serialization;
import foundation.settings;
import engine.project; // manifest + layout (runtime-side, editor-free)
import foundation.vfs.pak; // dist mode: one Content.pak holds products + scenes + scripts

#include "PlayerApplication.h" // the shared runner (uses the imports above)

using namespace foundation::core;
namespace runtime = foundation::runtime;
namespace shell = foundation::shell;
namespace graphics = foundation::graphics;
using engine::player::PlayerApplication;
using engine::player::PlayerOptions;

extern "C" const char* BuildStamp();

int main(int argc, char** argv)
{
    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&consoleSink);
    LOG_INFO(u8"Build", u8"Player build {}",
                      reinterpret_cast<const char8_t*>(BuildStamp()));
    GlobalLogger().SetMinLevel(LogLevel::Info);

    PlayerOptions options;
    if (argc > 1 && argv[1][0] != '-')
    {
        options.projectDir = String(StringView(reinterpret_cast<const utf8char*>(argv[1])));
    }
    else
    {
        // No path given: behave like a SHIPPED game binary - the game is wherever we are.
        // Try the current directory, then the executable's own directory (double-click /
        // run-from-anywhere), then the dev default.
        namespace fs = std::filesystem;
        std::error_code ec;
        auto hasGame = [](const fs::path& dir)
        {
            std::error_code e;
            return fs::exists(dir / "Content.pak", e) || fs::exists(dir / "Project.xml", e);
        };
        const fs::path exeDir =
            fs::weakly_canonical(fs::absolute(fs::path(argv[0]), ec), ec).parent_path();
        fs::path chosen = ".";
        if (!hasGame(chosen) && hasGame(exeDir))
        {
            chosen = exeDir;
        }
        else if (!hasGame(chosen))
        {
            chosen = "EditorProject";
        } // dev fallback
        options.projectDir =
            String(StringView(reinterpret_cast<const utf8char*>(chosen.string().c_str())));
    }
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::strcmp(argv[i], "--scene") == 0)
        {
            options.sceneOverride =
                String(StringView(reinterpret_cast<const utf8char*>(argv[i + 1])));
        }
        if (std::strcmp(argv[i], "--exit-after") == 0)
        {
            options.exitAfterSeconds = static_cast<f32>(std::atof(argv[i + 1]));
        }
    }

    shell::WindowSettings ws;
    ws.title = u8"Player";
    ws.width = 1280;
    ws.height = 720;
    auto shellPtr = shell::CreateShell(ws);
    if (shellPtr.Get() == nullptr || shellPtr->MainWindow() == nullptr)
    {
        std::fprintf(stderr, "Engine.Player: failed to create the OS shell/window\n");
        return 1;
    }

    graphics::GraphicsDeviceDesc gdd;
    // Backend from the CLI (--vulkan default / --webgpu / --dx12), so the desktop player can drive the
    // SAME WebGPU backend the browser uses - with the DXC runtime compiler + shader hot-reload present,
    // which the web build lacks. Invaluable for debugging web-render issues without the wasm/export loop.
    gdd.backend = graphics::SelectBackendFromArguments(argc, argv);
    auto gpu = graphics::CreateGraphicsDevice(gdd);
    if (!gpu.HasValue())
    {
        std::fprintf(stderr, "Engine.Player: failed to create the graphics device\n");
        return 1;
    }

    PlayerApplication app(static_cast<PlayerOptions&&>(options));
    const int code = runtime::RunApplication(app, *shellPtr, gpu.Value().Get());
    GlobalLogger().RemoveSink(&consoleSink);
    return code;
}
