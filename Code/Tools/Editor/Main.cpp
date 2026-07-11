// Draconic Editor - the editor executable (docs/design/editor.md §3.1: the ASSEMBLY point).
// Creates the OS shell + graphics device and runs EditorApplication. Per-subsystem editor
// modules (draconic.<sys>.editor) get linked HERE and their RegisterEditor(EditorContext&)
// called on the app's context - the editor core/app libraries never link engine subsystems.
//
// Usage: RaptorEditor [projectDirectory]
//   Opens the project (scaffolding Project.xml + Content/Sources/Cooked/Editor/.cache on first
//   run). Defaults to ./EditorProject.

#include <cstdio>

import draconic.core;
import draconic.shell;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.desktop;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;
namespace shell = draconic::shell;
namespace graphics = draconic::graphics;
namespace runtime = draconic::runtime;
namespace edapp = draconic::editor::app;

int main(int argc, char** argv)
{
    edapp::EditorAppConfig config;
    config.projectDirectory = String(argc > 1
        ? StringView(reinterpret_cast<const utf8char*>(argv[1]))
        : StringView(u8"EditorProject"));
    config.fontPath = String(StringView(reinterpret_cast<const utf8char*>(DRACONIC_EDITOR_FONT_PATH)));

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
    return runtime::RunApplication(app, *shellPtr, gpu.Value().Get());
}
