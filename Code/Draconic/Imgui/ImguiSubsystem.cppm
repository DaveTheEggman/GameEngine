/// Draconic::Imgui — the `:subsystem` partition.
///
/// ImguiSubsystem: a Context-level subsystem that owns the Dear ImGui context + the RHI renderer and
/// exposes the per-frame hooks an app drives — NewFrame (feed input + display size, begin the UI frame)
/// and Render (record the built draw data onto the frame's target). Apps register it, call NewFrame at
/// the top of their update, build UI with ImGui:: directly, and call Render after the scene. The
/// subsystem is self-contained (its own DXC compiler + ShaderSystem) so it has no renderer dependency.

module;
#include "Core/Prelude.h"
#include "imgui.h"

export module draconic.imgui:subsystem;

import draconic.core;
import draconic.rhi;
import draconic.runtime;             // Subsystem
import draconic.shell;    // IInputManager / IMouse / IKeyboard / KeyCode / MouseButton
import draconic.runtime.graphics;    // FrameContext
import draconic.shaders;
import draconic.shaders.system;
import :renderer;

using namespace draconic::core;
namespace rhi = draconic::rhi;
namespace shell = draconic::shell;

export namespace draconic::imgui {

class ImguiSubsystem final : public draconic::runtime::Subsystem {
public:
    ImguiSubsystem(rhi::Device& device, u32 framesInFlight) noexcept
        : m_device(&device), m_framesInFlight(framesInFlight < 1 ? 1 : framesInFlight) {}

    // Begin a UI frame: stamp display size (cached from the last Render) + dt, feed input, NewFrame.
    // Call at the top of the app's update, before any ImGui:: widget calls.
    void NewFrame(shell::IInputManager* input, f32 deltaTime) {
        if (!m_ready) { return; }
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_width), static_cast<float>(m_height));
        io.DeltaTime   = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);
        FeedInput(io, input);
        ImGui::NewFrame();
        m_frameOpen = true;
    }

    // Finish the UI frame + render it onto the frame's backbuffer (drawn over the scene). Call after the
    // app has rendered its scene to the backbuffer, before present.
    void Render(draconic::runtime::FrameContext& frame) {
        if (!m_ready) { return; }
        if (!m_frameOpen) { ImGui::NewFrame(); }   // keep ImGui balanced even if NewFrame was skipped
        m_frameOpen = false;
        ImGui::Render();
        m_width  = frame.width; m_height = frame.height;
        if (frame.encoder == nullptr || frame.backbufferView == nullptr || frame.window == nullptr) { return; }
        const rhi::TextureFormat fmt = frame.window->Swap()->Format();
        m_renderer->Render(*frame.encoder, frame.backbufferView, fmt, frame.width, frame.height,
                           ImGui::GetDrawData(), frame.frameIndex);
    }

    [[nodiscard]] bool Ready() const noexcept { return m_ready; }

protected:
    void OnInit() override {
        if (!shaders::createCompiler(shaders::CompilerDesc{}, m_compiler).IsOk() || m_compiler == nullptr) { return; }
        m_shaders = MakeUnique<shaders::ShaderSystem>(DefaultAllocator(), *m_compiler, *m_device);

        IMGUI_CHECKVERSION();
        m_context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;                       // no imgui.ini persistence by default
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.Fonts->AddFontDefault();
        ImGui::StyleColorsDark();

        m_renderer = MakeUnique<ImguiRenderer>(DefaultAllocator(), *m_device, *m_shaders, m_framesInFlight);
        if (!m_renderer->Initialize().IsOk()) { m_renderer.Reset(); return; }
        m_ready = true;
    }

    void OnShutdown() override {
        if (m_device != nullptr) { m_device->WaitIdle(); }
        m_renderer.Reset();
        if (m_context != nullptr) { ImGui::DestroyContext(m_context); m_context = nullptr; }
        m_shaders.Reset();
        if (m_compiler != nullptr) { m_compiler->Destroy(); m_compiler = nullptr; }
        m_ready = false;
    }

private:
    void FeedInput(ImGuiIO& io, shell::IInputManager* input) {
        if (input == nullptr) { return; }
        if (shell::IMouse* m = input->Mouse()) {
            io.AddMousePosEvent(m->X(), m->Y());
            io.AddMouseButtonEvent(0, m->IsButtonDown(shell::MouseButton::Left));
            io.AddMouseButtonEvent(1, m->IsButtonDown(shell::MouseButton::Right));
            io.AddMouseButtonEvent(2, m->IsButtonDown(shell::MouseButton::Middle));
            const f32 sx = m->ScrollX(), sy = m->ScrollY();
            if (sx != 0.0f || sy != 0.0f) { io.AddMouseWheelEvent(sx, sy); }
        }
        if (shell::IKeyboard* k = input->Keyboard()) {
            using K = shell::KeyCode;
            io.AddKeyEvent(ImGuiMod_Ctrl,  k->IsKeyDown(K::LeftCtrl)  || k->IsKeyDown(K::RightCtrl));
            io.AddKeyEvent(ImGuiMod_Shift, k->IsKeyDown(K::LeftShift) || k->IsKeyDown(K::RightShift));
            io.AddKeyEvent(ImGuiMod_Alt,   k->IsKeyDown(K::LeftAlt)   || k->IsKeyDown(K::RightAlt));
            io.AddKeyEvent(ImGuiKey_Tab,        k->IsKeyDown(K::Tab));
            io.AddKeyEvent(ImGuiKey_LeftArrow,  k->IsKeyDown(K::Left));
            io.AddKeyEvent(ImGuiKey_RightArrow, k->IsKeyDown(K::Right));
            io.AddKeyEvent(ImGuiKey_UpArrow,    k->IsKeyDown(K::Up));
            io.AddKeyEvent(ImGuiKey_DownArrow,  k->IsKeyDown(K::Down));
            io.AddKeyEvent(ImGuiKey_Enter,      k->IsKeyDown(K::Return));
            io.AddKeyEvent(ImGuiKey_Escape,     k->IsKeyDown(K::Escape));
            io.AddKeyEvent(ImGuiKey_Backspace,  k->IsKeyDown(K::Backspace));
            io.AddKeyEvent(ImGuiKey_Delete,     k->IsKeyDown(K::Delete));
            io.AddKeyEvent(ImGuiKey_Space,      k->IsKeyDown(K::Space));
        }
    }

    rhi::Device*                     m_device;
    u32                              m_framesInFlight = 2;
    shaders::Compiler*               m_compiler = nullptr;
    UniquePtr<shaders::ShaderSystem> m_shaders;
    UniquePtr<ImguiRenderer>         m_renderer;
    ImGuiContext*                    m_context = nullptr;
    u32  m_width = 1280, m_height = 720;   // cached from the last Render (display size for NewFrame)
    bool m_ready = false;
    bool m_frameOpen = false;
};

} // namespace draconic::imgui
