// RaptorPlayer - the generic game runner (MVP-to-Export milestone, docs/design/roadmap.md).
//
// Runs a project with ZERO native game code: engine subsystems + the project's content +
// the default scene, simulating - and, when the manifest names one, the project's GAME SCRIPT
// (the scripted IApplication counterpart: a `Game` class in Wren OR AngelScript with
// launch/update(dt)/exit, orchestrating above scenes - the backend resolves by the script's
// language). With scripting, player + scripts + cooked content IS the game; projects that outgrow
// scripts graduate to a native IApplication at the same seam.
//
// This is the DESKTOP entry point (SDL shell + a --backend-selected device + the blocking desktop
// runner + CLI args). The browser sibling is WebMain.cpp; both share PlayerApplication.h.
//
// Usage: RaptorPlayer <projectDir> [--scene <source-db-path>] [--exit-after <seconds>]
//
// Two modes, detected by layout:
//   PROJECT dir (Project.xml): scenes load from the authored source DB (their cooked form IS
//     the authored form - scenes are builder-less by design), products resolve from Cooked/ -
//     the editor's own runtime path. The dev loop.
//   DIST dir (Content.pak + player.xml, staged by RaptorExport): ONE binary DB inside the pak
//     holds products AND scenes; the game script rides in the pak as a raw entry. Zero editor
//     code links into this binary - the shipping shape.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.shell;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.desktop;
import draconic.runtime.defaultapp;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.scene.resource;
import draconic.render;
import draconic.render.subsystem;
import draconic.animation;
import draconic.animation.resource;
import draconic.animation.subsystem;
import draconic.particles;
import draconic.particles.resource;
import draconic.particles.subsystem;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.audio;
import draconic.audio.resource;
import draconic.audio.subsystem;
import draconic.materials;
import draconic.materials.resource;
import draconic.texture;
import draconic.texture.resource;
import draconic.image.resource;
import draconic.model.resource;
import draconic.script;
import draconic.script.resource; // ScriptClass (the cooked game script, bound from the content DB)
import draconic.input;
import draconic.physics;
import draconic.physics.resource;
import draconic.physics.subsystem;
import draconic.input.resource;
import draconic.input.subsystem;
import draconic.ui.resource;  // UITheme (the manifest's default theme)
import draconic.ui.subsystem; // UISubsystem (IME target + default theme)
import draconic.xml.serialization;
import draconic.settings;
import draconic.project; // manifest + layout (runtime-side, editor-free)
import draconic.vfs.pak; // dist mode: one Content.pak holds products + scenes + scripts

#include "PlayerApplication.h" // the shared runner (uses the imports above)

using namespace draconic::core;
namespace runtime = draconic::runtime;
namespace shell = draconic::shell;
namespace graphics = draconic::graphics;
using draconic::player::PlayerApplication;
using draconic::player::PlayerOptions;

int main(int argc, char** argv)
{
    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&consoleSink);
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
    ws.title = u8"Raptor Player";
    ws.width = 1280;
    ws.height = 720;
    auto shellPtr = shell::CreateShell(ws);
    if (shellPtr.Get() == nullptr || shellPtr->MainWindow() == nullptr)
    {
        std::fprintf(stderr, "RaptorPlayer: failed to create the OS shell/window\n");
        return 1;
    }

    graphics::GraphicsDeviceDesc gdd;
    gdd.backend = graphics::BackendType::Vulkan;
    auto gpu = graphics::CreateGraphicsDevice(gdd);
    if (!gpu.HasValue())
    {
        std::fprintf(stderr, "RaptorPlayer: failed to create the graphics device\n");
        return 1;
    }

    PlayerApplication app(static_cast<PlayerOptions&&>(options));
    const int code = runtime::RunApplication(app, *shellPtr, gpu.Value().Get());
    GlobalLogger().RemoveSink(&consoleSink);
    return code;
}
