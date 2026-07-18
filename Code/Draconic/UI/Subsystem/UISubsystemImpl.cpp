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
import draconic.render;
import draconic.render.subsystem;   // ExtractPrimaryCamera (billboard projection)

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

        [[nodiscard]] vgr::VGRenderer* RendererFor(rhi::TextureFormat format)
        {
            for (auto& entry : renderers)
            {
                if (entry.format == format) { return entry.renderer.Get(); }
            }
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
            return renderers[renderers.Size() - 1].renderer.Get();
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
        m_screenRoot = MakeRef<RootView>(DefaultAllocator());
        m_context.AddRootView(m_screenRoot.Get());
        // Billboards live in one shared absolute layer BELOW the canvases (one batch).
        auto layer = MakeRef<AbsoluteLayout>(DefaultAllocator());
        layer->IsHitTestVisible = false;   // nameplates never eat clicks (P2 scope)
        m_billboardLayer = layer;
        m_screenRoot->AddView(m_billboardLayer.Get());
        // The scene-LESS screen tier (global overlays) sits above everything; canvas
        // attaches re-append it so it stays topmost (draw order = child order). It only
        // hit-tests while it HOLDS overlays - an empty full-screen layer must never
        // swallow the clicks meant for the canvases below it.
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
        }
    }

    void UISubsystem::OnShutdown()
    {
        if (draconic::runtime::Context* context = GetContext())
        {
            if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
            {
                scenes->UnregisterSceneAware(this);
            }
        }
        m_billboardLayer = nullptr;
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
        for (dscene::Scene* scene : m_scenes)
        {
            auto* canvases = scene->GetSystem<UICanvasComponentManager>();
            if (canvases == nullptr) { continue; }
            canvases->ForEach([&](UICanvasComponent& c, dscene::EntityHandle) {
                const UIDocument* document = c.document.Get();
                if (document != c.builtFrom)
                {
                    if (c.root.Get() != nullptr)
                    {
                        m_screenRoot->RemoveView(c.root.Get());
                        c.root = nullptr;
                    }
                    if (document != nullptr && !document->markup.IsEmpty())
                    {
                        c.root = MarkupLoader::LoadFromString(document->markup.AsView(), &m_context);
                        if (c.root.Get() != nullptr)
                        {
                            m_screenRoot->AddView(c.root.Get());
                            if (m_overlayLayer.Get() != nullptr)
                            {
                                RefPtr<View> keep = m_overlayLayer;
                                m_screenRoot->RemoveView(keep.Get());
                                m_screenRoot->AddView(keep.Get());
                            }
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
                        m_billboardLayer->RemoveView(c.root.Get());
                        c.root = nullptr;
                    }
                    if (document != nullptr && !document->markup.IsEmpty())
                    {
                        c.root = MarkupLoader::LoadFromString(document->markup.AsView(), &m_context);
                        if (c.root.Get() != nullptr)
                        {
                            auto lp = MakeRef<AbsoluteLayoutParams>(DefaultAllocator());
                            c.root->LayoutParams = lp;
                            m_billboardLayer->AddView(c.root.Get());
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
        if (m_overlayLayer.Get() != nullptr)
        {
            m_overlayLayer->IsHitTestVisible = m_overlayLayer->ChildCount() > 0;
        }
        // The SAME facades the action layer evaluates: window coords in the player,
        // content coords in the Game tab (the InputSurface transform) - transparently.
        if (m_input == nullptr) { return; }
        draconic::input::IInputSourceProvider& devices = m_input->ActiveSource();
        draconic::shell::IMouse* mouse = devices.Mouse();
        InputManager& inputManager = *m_context.GetInputManager();
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
        // UI-consumed input never reaches gameplay actions.
        bool pointer = false;
        if (mouse != nullptr && m_screenRoot.Get() != nullptr)
        {
            View* hit = m_screenRoot->HitTest(Float2{ mouse->X(), mouse->Y() });
            pointer = hit != nullptr && hit != m_screenRoot.Get();
        }
        pointer = pointer || inputManager.PressedId() != ViewId{};
        const bool keyboard = m_context.WantsTextInput();
        m_pointerConsumed = pointer;
        m_input->Runtime().SetConsumptionMask(
            draconic::input::ActionRuntime::ConsumptionMask{ pointer, keyboard });
    }

    void UISubsystem::RenderOverlay(dscene::Scene& scene, rhi::CommandEncoder& encoder,
                                    rhi::TextureView* target, rhi::TextureFormat format,
                                    u32 width, u32 height, i32 frameIndex)
    {
        if (m_screenRoot.Get() == nullptr || target == nullptr || width == 0 || height == 0)
        {
            return;
        }

        // Per-scene gating: only THIS scene's canvases/billboards draw into this target.
        for (dscene::Scene* owned : m_scenes)
        {
            const bool active = owned == &scene;
            if (auto* canvases = owned->GetSystem<UICanvasComponentManager>())
            {
                canvases->ForEach([&](UICanvasComponent& c, dscene::EntityHandle) {
                    if (c.root.Get() != nullptr)
                    {
                        c.root->Visibility = (active && c.visible) ? VisibilityValue::Visible
                                                                   : VisibilityValue::Gone;
                    }
                });
            }
            if (auto* billboards = owned->GetSystem<UIBillboardComponentManager>())
            {
                billboards->ForEach([&](UIBillboardComponent& c, dscene::EntityHandle) {
                    if (c.root.Get() != nullptr && (!active || !c.visible))
                    {
                        c.root->Visibility = VisibilityValue::Gone;
                    }
                });
            }
        }

        // Billboards: project this scene's anchors through its primary camera (the
        // reference recipe: world -> clip -> NDC -> screen px; behind-camera parks at
        // (-10000,-10000); distance scale as a 2D view-transform).
        draconic::render::ViewCamera camera;
        const bool hasCamera = draconic::render::ExtractPrimaryCamera(scene, camera);
        if (auto* billboards = scene.GetSystem<UIBillboardComponentManager>())
        {
            const Float4x4 viewProjection = camera.ViewProjection();
            billboards->ForEach([&](UIBillboardComponent& c, dscene::EntityHandle e) {
                if (c.root.Get() == nullptr || !c.visible) { return; }
                if (!hasCamera)
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
                    Float4{ worldPos.x, worldPos.y, worldPos.z, 1.0f } * viewProjection;
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
                    lp->X = (ndcX * 0.5f + 0.5f) * static_cast<f32>(width);
                    lp->Y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<f32>(height);
                }
                f32 scale = 1.0f;
                if (c.scaleMode == BillboardScale::Distance)
                {
                    const Float3 toCamera{ worldPos.x - camera.position.x,
                                           worldPos.y - camera.position.y,
                                           worldPos.z - camera.position.z };
                    const f32 distance = Max(Length(toCamera), 0.001f);
                    scale = Clamp(c.referenceDistance / distance, c.minScale, c.maxScale);
                }
                c.root->Transform.Scale = Float2{ scale, scale };
            });
        }
        if (m_render.Get() == nullptr)
        {
            draconic::runtime::Context* context = GetContext();
            (void)context;
            m_render = MakeUnique<RenderState>(DefaultAllocator(), m_fonts.Get());
        }
        if (m_render->device == nullptr)
        {
            // Lazily wire the device from the first caller's encoder? The device comes
            // from the graphics host - callers set it via EnsureDevice below.
            return;
        }

        DrawRootInto(*m_screenRoot, encoder, target, format, width, height, frameIndex);
    }

    // Shared draw body: layout the root at the target size, batch through the shared
    // VGContext, draw via the per-format renderer in a Load-op pass.
    void UISubsystem::DrawRootInto(RootView& root, rhi::CommandEncoder& encoder,
                                   rhi::TextureView* target, rhi::TextureFormat format,
                                   u32 width, u32 height, i32 frameIndex)
    {
        root.ViewportSize = Float2{ static_cast<f32>(width), static_cast<f32>(height) };
        m_context.UpdateRootView(&root);

        m_render->vgContext.Clear();
        m_context.DrawRootView(&root, m_render->vgContext);
        vg::VGBatch& batch = m_render->vgContext.GetBatch();
        if (batch.commands.IsEmpty()) { return; }

        vgr::VGRenderer* renderer = m_render->RendererFor(format);
        if (renderer == nullptr) { return; }
        renderer->BeginFrame(frameIndex);
        const vgr::VGRenderSlice slice = renderer->Prepare(batch, frameIndex, width, height);

        rhi::RenderPassDesc pass;
        rhi::ColorAttachment color;
        color.view = target;
        color.loadOp = rhi::LoadOp::Load;
        color.storeOp = rhi::StoreOp::Store;
        pass.colorAttachments.Add(color);
        if (rhi::RenderPassEncoder* rp = encoder.BeginRenderPass(pass))
        {
            renderer->Render(*rp, width, height, frameIndex, slice);
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
        // the screen root - RenderOverlay only draws m_screenRoot, so previews cannot
        // appear in game targets; input stays with the ActiveInputRoot (view-only).
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
