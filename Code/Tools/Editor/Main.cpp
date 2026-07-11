// Draconic Editor - the editor executable (docs/design/editor.md §3.1: the ASSEMBLY point).
// Creates the OS shell + graphics device and runs EditorApplication. Per-subsystem editor
// modules (draconic.<sys>.editor) get linked HERE and their RegisterEditor(EditorContext&)
// called on the app's context - the editor core/app libraries never link engine subsystems.
//
// Usage: RaptorEditor [projectDirectory]
//   Opens the project (scaffolding Project.xml + Content/Sources/Cooked/Editor/.cache on first
//   run). Defaults to ./EditorProject.

#include <cstdio>
#include "Core/Log/Log.h"

import draconic.core;
import draconic.shell;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.desktop;
import draconic.scene.subsystem;
import draconic.render.subsystem;
import draconic.animation.subsystem;
import draconic.ui.runtime;
import draconic.editor.core;
import draconic.editor.app;
import draconic.editor.scene;

using namespace draconic::core;
namespace shell = draconic::shell;
namespace graphics = draconic::graphics;
namespace runtime = draconic::runtime;
namespace edapp = draconic::editor::app;

int main(int argc, char** argv)
{
    // Log capture FIRST (design doc §3.10): the editor buffer + console output go on the global
    // logger before shell/device creation, so early startup logs reach the Console panel.
    draconic::editor::EditorLogBuffer logBuffer;
    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&logBuffer);
    GlobalLogger().AddSink(&consoleSink);
    GlobalLogger().SetMinLevel(LogLevel::Debug);   // the Console panel has a Debug filter toggle

    edapp::EditorAppConfig config;
    config.projectDirectory = String(argc > 1
        ? StringView(reinterpret_cast<const utf8char*>(argv[1]))
        : StringView(u8"EditorProject"));
    config.fontPath = String(StringView(reinterpret_cast<const utf8char*>(DRACONIC_EDITOR_FONT_PATH)));
    config.logBuffer = &logBuffer;

    // Assembly (design doc §3.1): THIS is where engine subsystems and per-subsystem editor
    // plugins are chosen - the editor core/app libraries never link engine modules.
    config.configureEngine = [](draconic::runtime::IApplicationHost& host) {
        host.Ctx().AddSubsystem<draconic::scene::SceneSubsystem>();
        host.Ctx().AddSubsystem<draconic::render::RenderSubsystem>(
            *host.Graphics()->Raw(), host.Graphics()->FramesInFlight());
        host.Ctx().AddSubsystem<draconic::animation::AnimationSubsystem>();
    };
    // One scene-renderer frame bracket shared by ALL scene pages (multi-view contract); pages
    // open it lazily, the app's end hook closes it before the UI samples the viewport targets.
    draconic::editor::SceneRenderCoordinator sceneRender;
    config.registerEditors = [&sceneRender](draconic::editor::EditorContext& ctx,
                                            draconic::runtime::IApplicationHost& host,
                                            draconic::ui::runtime::UIHost& uiHost) {
        draconic::editor::RegisterSceneEditor(ctx, host, uiHost, sceneRender);
    };
    config.endSceneRendering = [&sceneRender](draconic::runtime::IApplicationHost&,
                                              draconic::graphics::FrameContext& frame) {
        sceneRender.EndWindow(frame);
    };

    DRACONIC_LOG_INFO(u8"Editor", u8"starting (project: {})", config.projectDirectory);

    shell::WindowSettings ws;
    ws.title  = u8"Draconic Editor";
    ws.width  = 1600;
    ws.height = 900;

    auto shellPtr = shell::CreateShell(ws);
    if (shellPtr.Get() == nullptr || shellPtr->MainWindow() == nullptr)
    {
        std::fprintf(stderr, "RaptorEditor: failed to create the OS shell/window\n");
        return 1;
    }

    graphics::GraphicsDeviceDesc gdd;
    gdd.backend          = graphics::BackendType::Vulkan;
    gdd.enableValidation = true;
    auto gpu = graphics::CreateGraphicsDevice(gdd);
    if (!gpu.HasValue())
    {
        std::fprintf(stderr, "RaptorEditor: failed to create the graphics device\n");
        return 1;
    }

    edapp::EditorApplication app(static_cast<edapp::EditorAppConfig&&>(config));
    const int code = runtime::RunApplication(app, *shellPtr, gpu.Value().Get());

    // The sinks are stack-owned and about to die; detach before returning.
    GlobalLogger().RemoveSink(&logBuffer);
    GlobalLogger().RemoveSink(&consoleSink);
    return code;
}
