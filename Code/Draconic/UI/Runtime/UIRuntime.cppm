// Draconic::UIRuntime - the `draconic.ui.runtime` module.
//
// UIHost: the reusable bridge that draws draconic.ui on the runtime's multi-window graphics host
// (draconic.graphics). It owns ONE UIContext with N RootViews (one per window), compiles the shared VG
// shaders once, and gives each window its own VGContext + VGRenderer + InputSurface stashed on the
// RenderWindow as a UIWindowData (graphics::IRenderWindowData - the payload the host reserves for exactly
// this). Mirrors the Sedulous "one UIContext / N RootViews / per-window swapchain" model.
//
// TOOLKIT-FREE by design: it renders any RootView, so games (single-window) and the editor (multi-window)
// share it. The docking / floating-window workbench lives ABOVE this in draconic.ui.application, which is
// the only module that pulls in draconic.ui.toolkit.
//
// A game's IApplication owns a UIHost, AttachWindow(mainWindow, root) once, then calls Update(dt) from
// OnUpdate and RenderWindow(frame) from OnRenderWindow. No bespoke swapchain / VG bring-up in the app.

module;
#include "Core/Prelude.h"

export module draconic.ui.runtime;

import draconic.core;
import draconic.rhi;
import draconic.shaders;
import draconic.shell;
import draconic.graphics;
import draconic.vg;
import draconic.vg.renderer;
import draconic.fonts;
import draconic.ui;
import draconic.ui.shell;

namespace core     = draconic::core;
namespace rhi      = draconic::rhi;
namespace shaders  = draconic::shaders;
namespace shell    = draconic::shell;
namespace graphics = draconic::graphics;
namespace vg       = draconic::vg;
namespace fonts    = draconic::fonts;

// draconic::ui::runtime nests in draconic::ui, so UIContext / RootView / InputManager / UiInputBridge /
// ShellClipboard resolve unqualified.
export namespace draconic::ui::runtime
{
    using core::f32;
    using core::i32;
    using core::u32;
    using core::usize;

    /// Per-window UI payload stashed on a graphics::RenderWindow via SetData. Owns that window's VG
    /// context + renderer + input surface; shares ownership of its RootView with the UIContext.
    class UIWindowData final : public graphics::IRenderWindowData
    {
    public:
        core::RefPtr<RootView>               root;
        core::UniquePtr<vg::VGContext>       vg;
        vg::renderer::VGRenderer             renderer;
        core::UniquePtr<shell::InputSurface> surface;
    };

    /// Renders draconic.ui on the runtime graphics host. Construct once (compiles the VG shaders), attach
    /// a RootView per window, drive Update()/RenderWindow() from the app's OnUpdate/OnRenderWindow.
    class UIHost
    {
    public:
        UIHost(graphics::GraphicsDevice& device, shell::IShell& shellRef, fonts::IFontService& fontService)
            : m_device(&device), m_shell(&shellRef), m_fonts(&fontService)
        {
            CompileShaders();
            m_ctx.SetFontService(&fontService);
            m_router    = core::MakeUnique<shell::InputRouter>(core::DefaultAllocator(), shellRef.Input());
            m_bridge    = core::MakeUnique<UiInputBridge>(core::DefaultAllocator(), &m_ctx);
            m_clipboard = core::MakeUnique<ShellClipboard>(core::DefaultAllocator(), &shellRef);
            m_ctx.SetClipboard(m_clipboard.Get());
        }

        ~UIHost()
        {
            // Per-window VGRenderers are disposed with their RenderWindow's payload (freed by the host
            // after a GPU idle), not here. We only own the shared shaders + compiler.
            if (m_vs != nullptr) { m_device->Raw()->DestroyShaderModule(m_vs); }
            if (m_fs != nullptr) { m_device->Raw()->DestroyShaderModule(m_fs); }
            if (m_compiler != nullptr) { m_compiler->Destroy(); core::DefaultAllocator().Delete(m_compiler); }
        }

        UIHost(const UIHost&) = delete;
        UIHost& operator=(const UIHost&) = delete;

        /// The shared UI context (set the theme / stylesheet on it, look up focus, etc.).
        [[nodiscard]] UIContext& Context() noexcept { return m_ctx; }

        /// Background clear color behind the UI (the theme usually paints an opaque root over it).
        void SetClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) noexcept { m_clear = rhi::ClearColor(r, g, b, a); }

        /// Give a RenderWindow a RootView: builds its VGContext + VGRenderer (against the window's swap
        /// format + the device frame-ring) + InputSurface, and stashes the payload on the RenderWindow.
        void AttachWindow(graphics::RenderWindow* window, core::RefPtr<RootView> root)
        {
            if (window == nullptr || !root) { return; }

            auto data = core::MakeUnique<UIWindowData>(core::DefaultAllocator());
            data->root = root;
            data->vg   = core::MakeUnique<vg::VGContext>(core::DefaultAllocator(), m_fonts);
            data->renderer.Initialize(*m_device->Raw(), *m_vs, *m_fs,
                                      window->Swap()->Format(), static_cast<i32>(m_device->FramesInFlight()));

            const f32 w = static_cast<f32>(window->Window().Width());
            const f32 h = static_cast<f32>(window->Window().Height());
            const core::ContentFit fit{ core::Rectangle{ 0.0f, 0.0f, w, h }, core::Float2{ w, h }, core::FitMode::Stretch };
            data->surface = core::MakeUnique<shell::InputSurface>(
                core::DefaultAllocator(), m_shell->Input(), window->Window().Id(), fit);

            root->ViewportSize = core::Float2{ w, h };
            root->DpiScale     = window->Window().ContentScale();
            m_ctx.AddRootView(root.Get());
            m_router->AddSurface(data->surface.Get());
            m_bridge->SetTextInputTarget(&window->Window());   // IME target (last attach wins; refined per-window later)

            UIWindowData* raw = data.Get();
            window->SetData(static_cast<core::UniquePtr<UIWindowData>&&>(data));   // RenderWindow owns the payload
            m_attached.PushBack(Attached{ window, raw });
        }

        /// Logical detach: stop routing input to the window and remove its root from the context, so
        /// nothing draws or hit-tests it anymore. The GPU payload (VGRenderer etc.) is deliberately LEFT
        /// on the RenderWindow - it is freed when the host destroys the RenderWindow (CloseWindow), which
        /// WaitIdles first. Freeing it here would race the GPU (no idle). So the docking-destroy path is
        /// DetachWindow(rw) followed by IApplicationHost::CloseWindow(rw).
        void DetachWindow(graphics::RenderWindow* window)
        {
            for (usize i = 0; i < m_attached.Size(); ++i)
            {
                if (m_attached[i].window == window)
                {
                    UIWindowData* data = m_attached[i].data;
                    m_router->RemoveSurface(data->surface.Get());
                    m_ctx.RemoveRootView(data->root.Get());
                    m_attached.RemoveAt(i);
                    return;
                }
            }
        }

        /// Once per frame: pump each window's input, tick the context, lay out every root.
        void Update(f32 deltaTime)
        {
            m_router->Update();

            // Cross-window drag: while a drag is in flight and the cursor is over/from a SECONDARY window
            // (a floating dock window follows the cursor, so it sits under the pointer), route the drag to
            // the MAIN window using global-mouse -> main-relative coords so the main window's drop targets
            // (e.g. a DockManager) see the drag-over and accept the drop. Mirrors Sedulous's editor: the OS
            // window being under the cursor is bypassed; we feed the main window explicitly.
            DragDropManager* dd = m_ctx.DragDrop();
            Attached* mainW = m_attached.IsEmpty() ? nullptr : &m_attached[0];
            const u32 focusedId = (m_shell->Input() != nullptr) ? m_shell->Input()->FocusedWindow() : 0;
            const bool crossWindowDrag = (dd != nullptr && dd->IsDragging() && mainW != nullptr
                                          && focusedId != 0 && focusedId != mainW->window->Window().Id());

            // Route mouse to a SINGLE window per frame (Sedulous processes one active root, not all N):
            // pumping non-hovered windows re-asserts their frozen last mouse position, spuriously
            // re-hovering them and clobbering the shared hover/cursor state. Priority:
            //   1. cross-window drag  -> the MAIN window (float sits under the cursor; see above);
            //   2. a captured view    -> its window (a dock-window edge-resize, a slider drag, ...), fed
            //                            LIVE global-relative coords so it keeps working off-window;
            //   3. otherwise          -> the window under the pointer (normal surface pump, keeps scroll).
            Attached* captured = CapturedWindow();
            if (crossWindowDrag)
            {
                PumpWindowAtGlobal(*mainW);
            }
            else if (captured != nullptr)
            {
                PumpWindowAtGlobal(*captured);
            }
            else
            {
                const u32 hoverId = (m_shell->Input() != nullptr) ? m_shell->Input()->HoverWindow() : 0;
                Attached* hoverW = FindByWindowId(hoverId);
                if (hoverW == nullptr) { hoverW = mainW; }
                if (hoverW != nullptr)
                {
                    const f32 w = static_cast<f32>(hoverW->window->Window().Width());
                    const f32 h = static_cast<f32>(hoverW->window->Window().Height());
                    hoverW->data->surface->SetRegion(core::Rectangle{ 0.0f, 0.0f, w, h });
                    hoverW->data->surface->SetContentSize(core::Float2{ w, h });
                    m_ctx.SetActiveInputRoot(hoverW->data->root.Get());
                    m_bridge->PumpFromSurface(*hoverW->data->surface);
                }
            }

            // Keyboard / text + IME follow the FOCUSED OS window: point the active input root and the
            // text-input target at the focused window's root, then dispatch its key/text events.
            if (shell::IInputManager* input = m_shell->Input())
            {
                Attached* focused = FindByWindowId(input->FocusedWindow());
                if (focused == nullptr && !m_attached.IsEmpty()) { focused = &m_attached[0]; }   // fallback: main
                if (focused != nullptr)
                {
                    m_ctx.SetActiveInputRoot(focused->data->root.Get());
                    m_bridge->SetTextInputTarget(&focused->window->Window());
                }
                for (const shell::InputEvent& ev : input->Events())
                {
                    switch (ev.kind)
                    {
                        case shell::InputEventKind::KeyDown:
                        case shell::InputEventKind::KeyUp:
                        case shell::InputEventKind::TextInput:
                            m_bridge->Dispatch(ev);
                            break;
                        default:
                            break;
                    }
                }
            }

            // Keep OS mouse events flowing across window edges while a drag or a captured interaction
            // (dock-window resize, slider, ...) is in progress - without an app-global capture the OS stops
            // delivering events once the cursor leaves the window, stalling multi-window drag/resize at the
            // edge and losing a release that happens outside the window.
            if (m_shell->Input() != nullptr)
            {
                if (shell::IMouse* mouse = m_shell->Input()->Mouse())
                {
                    const bool capturing = (dd != nullptr && dd->IsDragging())
                                        || (m_ctx.GetFocusManager()->CapturedView() != nullptr);
                    mouse->SetGlobalCapture(capturing);

                    // Push the hovered view's cursor to the OS (borderless floats have no WM to show resize
                    // grips). The UI<->shell cursor mapping lives in the bridge, with the rest of the enum
                    // translation.
                    m_bridge->SyncCursor(*mouse);
                }
            }

            m_ctx.BeginFrame(deltaTime);
            for (Attached& a : m_attached)
            {
                a.data->root->ViewportSize = core::Float2{
                    static_cast<f32>(a.window->Window().Width()),
                    static_cast<f32>(a.window->Window().Height()) };
                m_ctx.UpdateRootView(a.data->root.Get());
            }
        }

        /// Draw one window's RootView into its frame backbuffer. No-op for a non-UI or invalid frame.
        void RenderWindow(graphics::FrameContext& frame)
        {
            if (!frame.valid) { return; }
            UIWindowData* data = Find(frame.window);
            if (data == nullptr) { return; }

            data->vg->Clear();
            m_ctx.DrawRootView(data->root.Get(), *data->vg);
            vg::VGBatch& batch = data->vg->GetBatch();

            data->renderer.BeginFrame(static_cast<i32>(frame.frameIndex));
            const vg::renderer::VGRenderSlice slice =
                data->renderer.Prepare(batch, static_cast<i32>(frame.frameIndex), frame.width, frame.height);

            rhi::RenderPassEncoder* rp = frame.BeginBackbufferPass(m_clear);
            if (rp != nullptr)
            {
                data->renderer.Render(*rp, frame.width, frame.height, static_cast<i32>(frame.frameIndex), slice);
            }
            frame.EndBackbufferPass();
        }

    private:
        struct Attached
        {
            graphics::RenderWindow* window = nullptr;
            UIWindowData*           data   = nullptr;   // borrowed (the RenderWindow owns the payload)
        };

        [[nodiscard]] UIWindowData* Find(graphics::RenderWindow* window)
        {
            for (Attached& a : m_attached) { if (a.window == window) { return a.data; } }
            return nullptr;
        }

        [[nodiscard]] Attached* FindByWindowId(u32 windowId)
        {
            if (windowId == 0) { return nullptr; }
            for (Attached& a : m_attached) { if (a.window->Window().Id() == windowId) { return &a; } }
            return nullptr;
        }

        // The attached window that owns the currently mouse-captured view (a dock-window edge-resize, a
        // slider drag, ...), or null. Input must keep flowing to it even as the cursor leaves the window.
        [[nodiscard]] Attached* CapturedWindow()
        {
            View* captured = m_ctx.GetFocusManager()->CapturedView();
            if (captured == nullptr) { return nullptr; }
            RootView* capRoot = captured->Root();
            for (Attached& a : m_attached) { if (a.data->root.Get() == capRoot) { return &a; } }
            return nullptr;
        }

        // Pump one window's mouse using LIVE desktop-global coords mapped to that window's local space, so a
        // captured resize/drag keeps updating even when the cursor leaves the window (the surface freezes
        // its position when not hovered). Button state comes from the raw mouse, so the release ends it.
        void PumpWindowAtGlobal(Attached& a)
        {
            const f32 w = static_cast<f32>(a.window->Window().Width());
            const f32 h = static_cast<f32>(a.window->Window().Height());
            a.data->surface->SetRegion(core::Rectangle{ 0.0f, 0.0f, w, h });
            a.data->surface->SetContentSize(core::Float2{ w, h });
            m_ctx.SetActiveInputRoot(a.data->root.Get());

            shell::IMouse* mouse = (m_shell->Input() != nullptr) ? m_shell->Input()->Mouse() : nullptr;
            if (mouse == nullptr) { return; }
            const f32 mx = mouse->GlobalX() - static_cast<f32>(a.window->Window().X());
            const f32 my = mouse->GlobalY() - static_cast<f32>(a.window->Window().Y());
            m_bridge->PumpMouseAt(mx, my, mouse);
        }

        void CompileShaders()
        {
            (void)shaders::createCompiler(shaders::CompilerDesc{}, m_compiler);
            if (m_compiler == nullptr) { return; }
            CompileOne(vg::renderer::VertexShaderSource(),   shaders::ShaderStage::Vertex,   u8"vg.vert", m_vs);
            CompileOne(vg::renderer::FragmentShaderSource(), shaders::ShaderStage::Fragment, u8"vg.frag", m_fs);
        }

        // Replicates the sample framework's CompileToModule so draconic.ui.runtime doesn't depend on it:
        // DXIL for DX12, else SPIR-V with Vulkan binding shifts.
        void CompileOne(core::StringView src, shaders::ShaderStage stage, core::StringView label, rhi::ShaderModule*& out)
        {
            rhi::Device* device = m_device->Raw();
            const bool isDX12 = (device->type == rhi::DeviceType::DX12);
            const shaders::ShaderTarget target = isDX12 ? shaders::ShaderTarget::DXIL : shaders::ShaderTarget::SPIRV;

            shaders::CompileOptions opts{};
            opts.shaderModel       = u8"6_0";
            opts.optimizationLevel = 3;
            if (!isDX12)
            {
                opts.bindingShifts.constantBufferShift = 0;
                opts.bindingShifts.textureShift        = 1000;
                opts.bindingShifts.uavShift            = 2000;
                opts.bindingShifts.samplerShift        = 3000;
                opts.bindingShiftSets                  = 4;
            }

            shaders::CompileResult cr{};
            if (m_compiler->compile(reinterpret_cast<const core::u8*>(src.Data()), src.Size(),
                                    stage, u8"main", target, opts, cr) == core::ErrorCode::Ok)
            {
                rhi::ShaderModuleDesc desc{};
                desc.code  = core::Span<const core::u8>(cr.bytecode, cr.bytecodeSize);
                desc.label = label;
                (void)device->CreateShaderModule(desc, out);
            }
            m_compiler->freeResult(cr);
        }

        graphics::GraphicsDevice* m_device;   // borrowed
        shell::IShell*            m_shell;    // borrowed
        fonts::IFontService*      m_fonts;    // borrowed
        shaders::Compiler*        m_compiler = nullptr;
        rhi::ShaderModule*        m_vs = nullptr;
        rhi::ShaderModule*        m_fs = nullptr;

        UIContext m_ctx;   // shared context; owns N RootViews
        core::UniquePtr<shell::InputRouter> m_router;
        core::UniquePtr<UiInputBridge>      m_bridge;
        core::UniquePtr<ShellClipboard>     m_clipboard;
        core::Array<Attached>               m_attached;
        rhi::ClearColor m_clear = rhi::ClearColor(0.07f, 0.07f, 0.09f, 1.0f);
    };
}
