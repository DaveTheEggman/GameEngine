// Draconic::UISubsystem - implementation unit: VG/shader/render contact, the input pump,
// canvas syncing, and the component reflection bodies (GCC hygiene: none of this may sit
// in the interface's global fragment / partitions - see physics for the precedent).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"
#include <cmath>

module draconic.ui.subsystem;

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.resource;
import draconic.shell;
import draconic.rhi;
import draconic.fonts;
import draconic.fonts.ttf;
import draconic.input;
import draconic.input.subsystem;
import draconic.shaders;
import draconic.vg;
import draconic.vg.renderer;
import draconic.ui;
import draconic.ui.resource;
import draconic.render.api;
import draconic.render.subsystem;   // RenderSubsystem (overlay-role registration)

using namespace draconic::core;

namespace draconic::ui
{
    namespace vgr = draconic::vg::renderer;

    // Per-target-format VG pipeline (backbuffer vs viewport formats differ).
    struct UISubsystem::RenderState
    {
        draconic::shaders::Compiler* compiler = nullptr;
        rhi::Device* device = nullptr;
        rhi::ShaderModule* vertexShader = nullptr;
        rhi::ShaderModule* fragmentShader = nullptr;
        vg::VGContext vgContext;
        struct FormatRenderer
        {
            rhi::TextureFormat format = rhi::TextureFormat::RGBA8Unorm;
            UniquePtr<vgr::VGRenderer> renderer;
            u64 begunSerial = 0;   // last UI frame this renderer's ring was reset for
        };
        Array<FormatRenderer> renderers;
        i32 frameCount = 2;

        explicit RenderState(draconic::fonts::IFontService* fonts) : vgContext(fonts) {}

        ~RenderState()
        {
            renderers.Clear();
            if (vertexShader != nullptr && device != nullptr) { device->DestroyShaderModule(vertexShader); }
            if (fragmentShader != nullptr && device != nullptr) { device->DestroyShaderModule(fragmentShader); }
            if (compiler != nullptr) { compiler->Destroy(); }
        }

        bool CompileOne(StringView source, draconic::shaders::ShaderStage stage,
                        StringView label, rhi::ShaderModule*& out)
        {
            // The UIHost recipe: DXIL for DX12, else SPIR-V with Vulkan binding shifts.
            const bool isDX12 = (device->type == rhi::DeviceType::DX12);
            const draconic::shaders::ShaderTarget target =
                isDX12 ? draconic::shaders::ShaderTarget::DXIL
                       : draconic::shaders::ShaderTarget::SPIRV;
            draconic::shaders::CompileOptions options{};
            options.shaderModel = u8"6_0";
            options.optimizationLevel = 3;
            if (!isDX12)
            {
                options.bindingShifts.constantBufferShift = 0;
                options.bindingShifts.textureShift = 1000;
                options.bindingShifts.uavShift = 2000;
                options.bindingShifts.samplerShift = 3000;
                options.bindingShiftSets = 4;
            }
            draconic::shaders::CompileResult compiled{};
            bool ok = false;
            if (compiler->compile(reinterpret_cast<const u8*>(source.Data()), source.Size(),
                                  stage, u8"main", target, options, compiled) == ErrorCode::Ok)
            {
                rhi::ShaderModuleDesc desc{};
                desc.code = Span<const u8>(compiled.bytecode, compiled.bytecodeSize);
                desc.label = label;
                ok = device->CreateShaderModule(desc, out).IsOk();
            }
            compiler->freeResult(compiled);
            return ok;
        }

        // Fetch (or lazily create) the renderer for a target format, with its per-frame
        // ring reset EXACTLY once per UI frame (`frameSerial`): BeginFrame rewinds the
        // vertex/uniform rings and clears the command list, so calling it before every
        // draw would clobber the slices of draws recorded earlier in the SAME frame
        // (scene overlay + screen overlay + preview all share a format's renderer now).
        [[nodiscard]] vgr::VGRenderer* RendererFor(rhi::TextureFormat format, u64 frameSerial,
                                                   i32 frameIndex)
        {
            FormatRenderer* found = nullptr;
            for (auto& entry : renderers)
            {
                if (entry.format == format) { found = &entry; break; }
            }
            if (found == nullptr)
            {
                if (vertexShader == nullptr || fragmentShader == nullptr) { return nullptr; }
                auto renderer = MakeUnique<vgr::VGRenderer>(DefaultAllocator());
                if (!renderer->Initialize(*device, *vertexShader, *fragmentShader, format,
                                          frameCount).IsOk())
                {
                    return nullptr;
                }
                FormatRenderer entry;
                entry.format = format;
                entry.renderer = Move(renderer);
                renderers.PushBack(Move(entry));
                found = &renderers[renderers.Size() - 1];
            }
            if (found->begunSerial != frameSerial)
            {
                found->renderer->BeginFrame(frameIndex);
                found->begunSerial = frameSerial;
            }
            return found->renderer.Get();
        }
    };

    UISubsystem::UISubsystem() = default;
    UISubsystem::~UISubsystem() = default;

    void UISubsystem::OnInit()
    {
        MarkupLoader::Initialize();
        m_fonts = MakeUnique<draconic::fonts::TrueTypeFontService>(DefaultAllocator());
        StringView fontPath = m_fontPath.AsView();
        if (fontPath.IsEmpty())
        {
            fontPath = u8"Data/Assets/fonts/roboto/Roboto-Regular.ttf";   // dev-tree default
        }
        if (m_fonts->LoadFont(u8"Roboto", fontPath) == draconic::fonts::FontLoadResult::Success)
        {
            m_fonts->SetDefaultFamily(u8"Roboto");
        }
        else
        {
            DRACONIC_LOG_WARNING(u8"UI", u8"default font '{}' not loaded - game UI text will not render",
                                 fontPath);
        }
        m_context.SetFontService(m_fonts.Get());
        m_theme = GameTheme::Create();
        m_context.SetStyleSheet(m_theme);
        // The scene-LESS screen tier: the screen root holds ONLY the global overlay
        // layer (each scene's canvases + billboards live in that scene's own root). It
        // only hit-tests while it HOLDS overlays - an empty full-screen layer must never
        // swallow the clicks meant for the scene canvases below it.
        m_screenRoot = MakeRef<RootView>(DefaultAllocator());
        m_context.AddRootView(m_screenRoot.Get());
        auto overlay = MakeRef<FrameLayout>(DefaultAllocator());
        overlay->IsHitTestVisible = false;
        m_overlayLayer = overlay;
        m_screenRoot->AddView(m_overlayLayer.Get());
    }

    void UISubsystem::OnReady()
    {
        if (draconic::runtime::Context* context = GetContext())
        {
            m_input = context->GetSubsystem<draconic::input::InputSubsystem>();
            if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
            {
                scenes->RegisterSceneAware(this);   // injects the canvas manager per scene
            }
            // The overlay roles: scene tier draws inside the compose per view; screen
            // tier draws when the host calls RenderOverlays per window target. Headless
            // contexts (tests, cooker) have no render subsystem - both stay unregistered.
            if (auto* render = context->GetSubsystem<draconic::render::RenderSubsystem>())
            {
                m_sceneRenderer = render;
                m_screenRenderer = render;
                m_sceneRenderer->RegisterOverlay(static_cast<draconic::render::ISceneOverlay*>(this));
                m_screenRenderer->RegisterOverlay(static_cast<draconic::render::IScreenOverlay*>(this));
            }
        }
    }

    void UISubsystem::OnShutdown()
    {
        if (m_sceneRenderer != nullptr)
        {
            m_sceneRenderer->UnregisterOverlay(static_cast<draconic::render::ISceneOverlay*>(this));
            m_sceneRenderer = nullptr;
        }
        if (m_screenRenderer != nullptr)
        {
            m_screenRenderer->UnregisterOverlay(static_cast<draconic::render::IScreenOverlay*>(this));
            m_screenRenderer = nullptr;
        }
        if (draconic::runtime::Context* context = GetContext())
        {
            if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
            {
                scenes->UnregisterSceneAware(this);
            }
        }
        for (SceneUI& ui : m_sceneUIs)
        {
            if (ui.root.Get() != nullptr) { m_context.RemoveRootView(ui.root.Get()); }
        }
        m_sceneUIs.Clear();
        m_overlayLayer = nullptr;
        if (m_screenRoot.Get() != nullptr) { m_context.RemoveRootView(m_screenRoot.Get()); }
        m_screenRoot = nullptr;
        m_render = nullptr;
        m_theme = nullptr;
    }

    void UISubsystem::BeginFrame(f32 deltaTime)
    {
        // UNSCALED time: menus animate while the game is paused (BeginFrame receives the
        // raw host dt - the whole reason this runs here and not in Update).
        ++m_frameSerial;   // one VG ring reset per UI frame (see RenderState::RendererFor)
        m_context.BeginFrame(deltaTime);
        m_navDeltaTime = deltaTime;
        SyncCanvases();
        PumpInput();
    }

    void UISubsystem::SyncCanvases()
    {
        // Instantiate/rebuild each canvas's view tree from its cooked document. Documents
        // are TEMPLATES: a fresh tree per canvas; a document reload (different product
        // pointer) rebuilds; theme overrides parse per canvas as a LOCAL stylesheet.
        // Canvases and billboards parent into THEIR SCENE's root (the scene tier) - a
        // Simulate page's UI can never bleed into the Game tab by construction.
        for (SceneUI& sceneUI : m_sceneUIs)
        {
            dscene::Scene* scene = sceneUI.scene;
            auto* canvases = scene->GetSystem<UICanvasComponentManager>();
            if (canvases == nullptr) { continue; }
            canvases->ForEach([&](UICanvasComponent& c, dscene::EntityHandle) {
                const UIDocument* document = c.document.Get();
                if (document != c.builtFrom)
                {
                    if (c.root.Get() != nullptr)
                    {
                        sceneUI.root->RemoveView(c.root.Get());
                        c.root = nullptr;
                    }
                    if (document != nullptr && !document->markup.IsEmpty())
                    {
                        c.root = MarkupLoader::LoadFromString(document->markup.AsView(), &m_context);
                        if (c.root.Get() != nullptr)
                        {
                            sceneUI.root->AddView(c.root.Get());
                        }
                        else
                        {
                            DRACONIC_LOG_WARNING(u8"UI", u8"canvas document failed to instantiate");
                        }
                    }
                    c.builtFrom = document;
                }
                const UITheme* theme = c.theme.Get();
                if (theme != c.themeFrom)
                {
                    c.themeSheet = nullptr;
                    if (theme != nullptr && !theme->stylesheet.IsEmpty())
                    {
                        StyleSheetLoader loader;
                        loader.SetPalette(GameTheme::Palette());
                        c.themeSheet = loader.Load(theme->stylesheet.AsView());
                    }
                    if (c.root.Get() != nullptr) { c.root->SetLocalStyleSheet(c.themeSheet); }
                    c.themeFrom = theme;
                }
                if (c.root.Get() != nullptr)
                {
                    c.root->Visibility = c.visible ? VisibilityValue::Visible : VisibilityValue::Gone;
                    c.root->IsHitTestVisible = c.interactive;
                }
            });

            auto* billboards = scene->GetSystem<UIBillboardComponentManager>();
            if (billboards == nullptr) { continue; }
            billboards->ForEach([&](UIBillboardComponent& c, dscene::EntityHandle) {
                const UIDocument* document = c.document.Get();
                if (document != c.builtFrom)
                {
                    if (c.root.Get() != nullptr)
                    {
                        sceneUI.billboardLayer->RemoveView(c.root.Get());
                        c.root = nullptr;
                    }
                    if (document != nullptr && !document->markup.IsEmpty())
                    {
                        c.root = MarkupLoader::LoadFromString(document->markup.AsView(), &m_context);
                        if (c.root.Get() != nullptr)
                        {
                            auto lp = MakeRef<AbsoluteLayoutParams>(DefaultAllocator());
                            c.root->LayoutParams = lp;
                            sceneUI.billboardLayer->AddView(c.root.Get());
                        }
                    }
                    c.builtFrom = document;
                }
            });
        }
    }

    void UISubsystem::PumpInput()
    {
        // The global-overlay layer eats input only while occupied (see OnInit).
        const bool overlayActive =
            m_overlayLayer.Get() != nullptr && m_overlayLayer->ChildCount() > 0;
        if (m_overlayLayer.Get() != nullptr)
        {
            m_overlayLayer->IsHitTestVisible = overlayActive;
        }
        // The SAME facades the action layer evaluates: window coords in the player,
        // content coords in the Game tab (the InputSurface transform) - transparently.
        if (m_input == nullptr) { return; }
        draconic::input::IInputSourceProvider& devices = m_input->ActiveSource();
        draconic::shell::IMouse* mouse = devices.Mouse();
        InputManager& inputManager = *m_context.GetInputManager();

        // The context dispatches input through ONE ActiveInputRoot (the UIHost multi-
        // window precedent) - with the tiers split across roots, pick it per frame:
        // an OCCUPIED screen tier is modal and always wins; otherwise the root under
        // the pointer; otherwise (pad/keyboard-only) the first scene root with canvas
        // content, so gamepad nav reaches a pause menu no pointer ever hovered.
        {
            RootView* target = nullptr;
            if (overlayActive) { target = m_screenRoot.Get(); }
            if (target == nullptr && mouse != nullptr)
            {
                const Float2 point{ mouse->X(), mouse->Y() };
                if (m_screenRoot.Get() != nullptr)
                {
                    View* hit = m_screenRoot->HitTest(point);
                    if (hit != nullptr && hit != m_screenRoot.Get()) { target = m_screenRoot.Get(); }
                }
                for (usize i = 0; target == nullptr && i < m_sceneUIs.Size(); ++i)
                {
                    RootView* root = m_sceneUIs[i].root.Get();
                    if (root == nullptr) { continue; }
                    View* hit = root->HitTest(point);
                    if (hit != nullptr && hit != root) { target = root; }
                }
            }
            if (target == nullptr)
            {
                for (SceneUI& ui : m_sceneUIs)
                {
                    // Beyond the billboard layer = at least one canvas instantiated.
                    if (ui.root.Get() != nullptr && ui.root->ChildCount() > 1)
                    {
                        target = ui.root.Get();
                        break;
                    }
                }
            }
            if (target == nullptr) { target = m_screenRoot.Get(); }
            if (target != nullptr) { m_context.SetActiveInputRoot(target); }
        }
        if (mouse != nullptr)
        {
            const f32 x = mouse->X();
            const f32 y = mouse->Y();
            inputManager.ProcessMouseMove(x, y);
            const draconic::shell::MouseButton shellButtons[3] = {
                draconic::shell::MouseButton::Left, draconic::shell::MouseButton::Right,
                draconic::shell::MouseButton::Middle };
            const MouseButton uiButtons[3] = {
                MouseButton::Left, MouseButton::Right, MouseButton::Middle };
            for (u32 i = 0; i < 3; ++i)
            {
                const bool down = mouse->IsButtonDown(shellButtons[i]);
                if (down && !m_prevButtons[i])
                {
                    inputManager.ProcessMouseDown(uiButtons[i], x, y, m_context.TotalTime());
                }
                else if (!down && m_prevButtons[i])
                {
                    inputManager.ProcessMouseUp(uiButtons[i], x, y);
                }
                m_prevButtons[i] = down;
            }
            const f32 wheel = mouse->ScrollY();   // per-frame delta already
            if (wheel != 0.0f)
            {
                inputManager.ProcessMouseWheel(x, y, mouse->ScrollX(), wheel);
            }
        }

        // ---- gamepad focus navigation (game-ui.md P2): dpad/left stick move focus
        // through the framework's geometric MoveFocus; South = Submit (synthesized
        // Return - the existing dispatch-first activation path), East = Cancel
        // (synthesized Escape). Hold-repeat: 0.4s initial, 0.12s after. The pad is
        // deliberately NOT a consumption class - gameplay pad actions keep working
        // (menus that want exclusivity push an input SET, the existing mechanism). ----
        draconic::shell::IGamepad* pad = devices.Gamepad(0);
        if (pad != nullptr && pad->Connected() && m_screenRoot.Get() != nullptr)
        {
            const f32 stickX = pad->Axis(draconic::shell::GamepadAxis::LeftX);
            const f32 stickY = pad->Axis(draconic::shell::GamepadAxis::LeftY);
            constexpr f32 kThreshold = 0.6f;
            const bool wants[4] = {
                pad->IsButtonDown(draconic::shell::GamepadButton::DPadUp) || stickY < -kThreshold,
                pad->IsButtonDown(draconic::shell::GamepadButton::DPadDown) || stickY > kThreshold,
                pad->IsButtonDown(draconic::shell::GamepadButton::DPadLeft) || stickX < -kThreshold,
                pad->IsButtonDown(draconic::shell::GamepadButton::DPadRight) || stickX > kThreshold,
            };
            const FocusDirection directions[4] = {
                FocusDirection::Up, FocusDirection::Down,
                FocusDirection::Left, FocusDirection::Right };
            FocusManager* focus = m_context.GetFocusManager();
            for (u32 i = 0; i < 4; ++i)
            {
                if (!wants[i]) { m_navHeld[i] = false; continue; }
                bool fire = false;
                if (!m_navHeld[i])
                {
                    fire = true;
                    m_navHeld[i] = true;
                    m_navRepeat[i] = 0.4f;
                }
                else
                {
                    m_navRepeat[i] -= m_navDeltaTime;
                    if (m_navRepeat[i] <= 0.0f)
                    {
                        fire = true;
                        m_navRepeat[i] = 0.12f;
                    }
                }
                if (!fire || focus == nullptr) { continue; }
                if (focus->FocusedView() == nullptr) { focus->FocusNext(); }   // bootstrap
                else { (void)focus->MoveFocus(directions[i]); }
            }
            if (pad->IsButtonPressed(draconic::shell::GamepadButton::South))
            {
                inputManager.ProcessKeyDown(KeyCode::Return, KeyModifiers::None, false,
                                            m_context.TotalTime());
                inputManager.ProcessKeyUp(KeyCode::Return, KeyModifiers::None,
                                          m_context.TotalTime());
            }
            if (pad->IsButtonPressed(draconic::shell::GamepadButton::East))
            {
                inputManager.ProcessKeyDown(KeyCode::Escape, KeyModifiers::None, false,
                                            m_context.TotalTime());
                inputManager.ProcessKeyUp(KeyCode::Escape, KeyModifiers::None,
                                          m_context.TotalTime());
            }
        }

        // Consumption: pointer = an INTERACTIVE canvas under the cursor (or a pressed
        // view); keyboard = a focused text editor. Published to the action layer -
        // UI-consumed input never reaches gameplay actions. The pointer probes the
        // screen tier first (topmost), then every scene root.
        bool pointer = false;
        if (mouse != nullptr)
        {
            const Float2 point{ mouse->X(), mouse->Y() };
            if (m_screenRoot.Get() != nullptr)
            {
                View* hit = m_screenRoot->HitTest(point);
                pointer = hit != nullptr && hit != m_screenRoot.Get();
            }
            for (usize i = 0; !pointer && i < m_sceneUIs.Size(); ++i)
            {
                RootView* root = m_sceneUIs[i].root.Get();
                if (root == nullptr) { continue; }
                View* hit = root->HitTest(point);
                pointer = hit != nullptr && hit != root;
            }
        }
        pointer = pointer || inputManager.PressedId() != ViewId{};
        const bool keyboard = m_context.WantsTextInput();
        m_pointerConsumed = pointer;
        m_input->Runtime().SetConsumptionMask(
            draconic::input::ActionRuntime::ConsumptionMask{ pointer, keyboard });
    }

    // The scene-tier per-view sync: canvas visibility from the authored flag, then
    // billboard projection through the VIEW's real camera (world -> clip -> NDC -> px;
    // behind-camera parks at (-10000,-10000); distance scale as a 2D view-transform).
    // Public + encoder-free so headless tests drive it with a synthetic view.
    void UISubsystem::UpdateSceneView(dscene::Scene& scene, const render::SceneOverlayView& view)
    {
        if (auto* canvases = scene.GetSystem<UICanvasComponentManager>())
        {
            canvases->ForEach([&](UICanvasComponent& c, dscene::EntityHandle) {
                if (c.root.Get() != nullptr)
                {
                    c.root->Visibility = c.visible ? VisibilityValue::Visible
                                                   : VisibilityValue::Gone;
                }
            });
        }
        if (auto* billboards = scene.GetSystem<UIBillboardComponentManager>())
        {
            billboards->ForEach([&](UIBillboardComponent& c, dscene::EntityHandle e) {
                if (c.root.Get() == nullptr) { return; }
                if (!c.visible)
                {
                    c.root->Visibility = VisibilityValue::Gone;
                    return;
                }
                c.root->Visibility = VisibilityValue::Visible;
                const Float4x4 entity = scene.GetWorldMatrix(e);
                Float3 worldPos;
                if (c.orientation == BillboardOrientation::Cylindrical)
                {
                    const Float3 anchor = TransformPoint(Float3{ 0, 0, 0 }, entity);
                    worldPos = Float3{ anchor.x + c.offset.x, anchor.y + c.offset.y,
                                       anchor.z + c.offset.z };
                }
                else
                {
                    worldPos = TransformPoint(c.offset, entity);
                }
                const Float4 clip =
                    Float4{ worldPos.x, worldPos.y, worldPos.z, 1.0f } * view.viewProjection;
                auto* lp = Cast<AbsoluteLayoutParams>(c.root->LayoutParams.Get());
                if (lp == nullptr) { return; }
                if (clip.w <= 0.0f)
                {
                    lp->X = -10000.0f;   // behind the camera: clipped + unhit, no tree churn
                    lp->Y = -10000.0f;
                }
                else
                {
                    const f32 ndcX = clip.x / clip.w;
                    const f32 ndcY = clip.y / clip.w;
                    lp->X = (ndcX * 0.5f + 0.5f) * static_cast<f32>(view.targetWidth);
                    lp->Y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<f32>(view.targetHeight);
                }
                f32 scale = 1.0f;
                if (c.scaleMode == BillboardScale::Distance)
                {
                    const Float3 toCamera{ worldPos.x - view.cameraPosition.x,
                                           worldPos.y - view.cameraPosition.y,
                                           worldPos.z - view.cameraPosition.z };
                    const f32 distance = Max(Length(toCamera), 0.001f);
                    scale = Clamp(c.referenceDistance / distance, c.minScale, c.maxScale);
                }
                c.root->Transform.Scale = Float2{ scale, scale };
            });
        }
    }

    // Scene tier (ISceneOverlay): called inside the compose's shared overlay pass, once
    // per view. Draws the view's scene root - matched by SceneKey - with the view's
    // REAL camera, so scene UI lands wherever the scene renders and billboards project
    // correctly in every viewport.
    void UISubsystem::Render(rhi::RenderPassEncoder& encoder, const render::SceneOverlayView& view)
    {
        if (m_render.Get() == nullptr || m_render->device == nullptr) { return; }
        if (view.targetWidth == 0 || view.targetHeight == 0) { return; }
        SceneUI* sceneUI = nullptr;
        for (SceneUI& ui : m_sceneUIs)
        {
            if (static_cast<const void*>(ui.scene) == view.sceneKey) { sceneUI = &ui; break; }
        }
        if (sceneUI == nullptr || sceneUI->root.Get() == nullptr) { return; }
        // Sub-rect views (split-screen): the VG renderer draws at the target origin, so
        // positioning inside a sub-rect needs a VG viewport seam (consult-first rule).
        const bool fullRect = view.viewportX == 0 && view.viewportY == 0 &&
                              view.viewportWidth == view.targetWidth &&
                              view.viewportHeight == view.targetHeight;
        if (!fullRect)
        {
            if (!m_subRectWarned)
            {
                m_subRectWarned = true;
                DRACONIC_LOG_WARNING(u8"UI",
                    u8"scene UI skipped for a sub-rect view (split-screen scene HUD needs a VG viewport seam)");
            }
            return;
        }
        UpdateSceneView(*sceneUI->scene, view);
        DrawRootInPass(*sceneUI->root, encoder, view.targetFormat,
                       view.targetWidth, view.targetHeight, static_cast<i32>(view.frameIndex));
    }

    // Screen tier (IScreenOverlay): called from the host's RenderOverlays per window
    // target, after the scene composed. Draws the scene-less global overlays.
    void UISubsystem::Render(rhi::RenderPassEncoder& encoder, const render::ScreenOverlayView& view)
    {
        if (m_render.Get() == nullptr || m_render->device == nullptr) { return; }
        if (m_screenRoot.Get() == nullptr || view.width == 0 || view.height == 0) { return; }
        DrawRootInPass(*m_screenRoot, encoder, view.targetFormat, view.width, view.height,
                       static_cast<i32>(view.frameIndex));
    }

    // Records one root into an ALREADY-ACTIVE render pass: layout at the target size,
    // batch through the shared VGContext, upload a slice (pure mapped-memory writes -
    // legal during pass recording), draw. The per-format renderer's ring resets once
    // per UI frame (m_frameSerial) so same-frame draws never clobber each other.
    void UISubsystem::DrawRootInPass(RootView& root, rhi::RenderPassEncoder& encoder,
                                     rhi::TextureFormat format, u32 width, u32 height,
                                     i32 frameIndex)
    {
        root.ViewportSize = Float2{ static_cast<f32>(width), static_cast<f32>(height) };
        m_context.UpdateRootView(&root);

        m_render->vgContext.Clear();
        m_context.DrawRootView(&root, m_render->vgContext);
        vg::VGBatch& batch = m_render->vgContext.GetBatch();
        if (batch.commands.IsEmpty()) { return; }

        vgr::VGRenderer* renderer = m_render->RendererFor(format, m_frameSerial, frameIndex);
        if (renderer == nullptr) { return; }
        const vgr::VGRenderSlice slice = renderer->Prepare(batch, frameIndex, width, height);
        renderer->Render(encoder, width, height, frameIndex, slice);
    }

    // Pass-owning draw body (the preview seam): opens its own Load-op pass on the
    // caller's encoder, then records through DrawRootInPass.
    void UISubsystem::DrawRootInto(RootView& root, rhi::CommandEncoder& encoder,
                                   rhi::TextureView* target, rhi::TextureFormat format,
                                   u32 width, u32 height, i32 frameIndex)
    {
        rhi::RenderPassDesc pass;
        rhi::ColorAttachment color;
        color.view = target;
        color.loadOp = rhi::LoadOp::Load;
        color.storeOp = rhi::StoreOp::Store;
        pass.colorAttachments.Add(color);
        if (rhi::RenderPassEncoder* rp = encoder.BeginRenderPass(pass))
        {
            DrawRootInPass(root, *rp, format, width, height, frameIndex);
            rp->End();
        }
    }

    RefPtr<RootView> UISubsystem::CreatePreview(const UIDocument& document)
    {
        if (document.markup.IsEmpty()) { return {}; }
        RefPtr<View> tree = MarkupLoader::LoadFromString(document.markup.AsView(), &m_context);
        if (tree.Get() == nullptr) { return {}; }
        RefPtr<RootView> root = MakeRef<RootView>(DefaultAllocator());
        root->AddView(tree.Get());
        // Registered on the GAME context (styles/fonts/ids resolve there) but NEVER on
        // the screen root or a scene root - the overlay roles only draw those, so
        // previews cannot appear in game targets; input stays with the active roots.
        m_context.AddRootView(root.Get());
        return root;
    }

    void UISubsystem::DestroyPreview(RootView* root)
    {
        if (root != nullptr) { m_context.RemoveRootView(root); }
    }

    void UISubsystem::RenderPreview(RootView& root, rhi::CommandEncoder& encoder,
                                    rhi::TextureView* target, rhi::TextureFormat format,
                                    u32 width, u32 height, i32 frameIndex)
    {
        if (target == nullptr || width == 0 || height == 0) { return; }
        if (m_render.Get() == nullptr || m_render->device == nullptr) { return; }
        DrawRootInto(root, encoder, target, format, width, height, frameIndex);
    }

    // Device/shader bring-up, called once by the application layer that owns graphics.
    void UISubsystem::EnsureRenderReady(rhi::Device& device, i32 frameCount)
    {
        if (m_render.Get() == nullptr)
        {
            m_render = MakeUnique<RenderState>(DefaultAllocator(), m_fonts.Get());
        }
        if (m_render->device != nullptr) { return; }
        m_render->device = &device;
        m_render->frameCount = frameCount;
        (void)draconic::shaders::createCompiler(draconic::shaders::CompilerDesc{}, m_render->compiler);
        if (m_render->compiler == nullptr)
        {
            DRACONIC_LOG_ERROR(u8"UI", u8"shader compiler unavailable - game UI will not render");
            return;
        }
        (void)m_render->CompileOne(vgr::VertexShaderSource(), draconic::shaders::ShaderStage::Vertex,
                                   u8"gameui.vg.vert", m_render->vertexShader);
        (void)m_render->CompileOne(vgr::FragmentShaderSource(), draconic::shaders::ShaderStage::Fragment,
                                   u8"gameui.vg.frag", m_render->fragmentShader);
    }
}

// ---- reflection (impl unit per the GCC rule) ----
namespace draconic::ui
{
    DRACONIC_REFLECT_ENUM(CanvasScalerMode, "draconic::ui")
    {
        builder.Value("ConstantPixel", CanvasScalerMode::ConstantPixel);
        builder.Value("ReferenceResolution", CanvasScalerMode::ReferenceResolution);
    }

    DRACONIC_REFLECT_ENUM(BillboardOrientation, "draconic::ui")
    {
        builder.Value("Screen", BillboardOrientation::Screen);
        builder.Value("Cylindrical", BillboardOrientation::Cylindrical);
    }

    DRACONIC_REFLECT_ENUM(BillboardScale, "draconic::ui")
    {
        builder.Value("Fixed", BillboardScale::Fixed);
        builder.Value("Distance", BillboardScale::Distance);
    }

    DRACONIC_REFLECT_VALUE(UIBillboardComponent, "draconic::ui")
    {
        builder.DataVersion(1);
        builder.Property<&UIBillboardComponent::document>("document");
        builder.Property<&UIBillboardComponent::offset>("offset");
        builder.Property<&UIBillboardComponent::orientation>("orientation");
        builder.Property<&UIBillboardComponent::scaleMode>("scaleMode");
        builder.Property<&UIBillboardComponent::referenceDistance>("referenceDistance");
        builder.Property<&UIBillboardComponent::minScale>("minScale");
        builder.Property<&UIBillboardComponent::maxScale>("maxScale");
        builder.Property<&UIBillboardComponent::visible>("visible");
    }

    DRACONIC_REFLECT_VALUE(UICanvasComponent, "draconic::ui")
    {
        builder.DataVersion(1);
        builder.Property<&UICanvasComponent::document>("document");
        builder.Property<&UICanvasComponent::theme>("theme");
        builder.Property<&UICanvasComponent::order>("order");
        builder.Property<&UICanvasComponent::visible>("visible");
        builder.Property<&UICanvasComponent::interactive>("interactive");
        builder.Property<&UICanvasComponent::scalerMode>("scalerMode");
        builder.Property<&UICanvasComponent::referenceResolution>("referenceResolution");
    }

    void RegisterUIComponentReflection()
    {
        static const bool once = []() {
            DraconicRegisterEnum_CanvasScalerMode();
            DraconicRegisterEnum_BillboardOrientation();
            DraconicRegisterEnum_BillboardScale();
            DraconicRegisterValue_UICanvasComponent();
            DraconicRegisterValue_UIBillboardComponent();
            return true;
        }();
        (void)once;
    }
}
