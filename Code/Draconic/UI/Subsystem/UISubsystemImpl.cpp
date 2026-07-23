// Draconic::UISubsystem - implementation unit: VG/shader/render contact, the input pump,
// canvas syncing, and the component reflection bodies (GCC hygiene: none of this may sit
// in the interface's global fragment / partitions - see physics for the precedent).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
    namespace vg = draconic::vg;

namespace draconic::ui
{

    // Per-canvas host inside a scene root: carries the canvas's draw ORDER (the scene
    // root's canvas children are kept sorted by it - higher = later = on top; the
    // billboard layer stays child 0, below every canvas) and the SCALER transform
    // (ReferenceResolution lays the document out at the reference size and uniform-
    // scales it to fit, centered; ConstantPixel = 1 UI px = 1 target px). Hit-test
    // transparent: only the document tree consumes input, and the child's scale
    // transform is undone by the standard ViewGroup inverse-transform hit test.
    class CanvasHostView final : public ViewGroup
    {
        DRACONIC_OBJECT(CanvasHostView, ViewGroup)
    public:
        i32 Order = 0;
        bool Seen = false;   // swept by SyncCanvases when the component vanished
        CanvasScalerMode ScalerMode = CanvasScalerMode::ConstantPixel;
        Float2 ReferenceResolution{ 0.0f, 0.0f };

        CanvasHostView() { IsHitTestVisible = false; }

        /// The size the document lays out at: the reference resolution when scaling,
        /// else the host's own (viewport) size.
        [[nodiscard]] Float2 LayoutSizeFor(f32 width, f32 height) const noexcept
        {
            if (ScalerMode == CanvasScalerMode::ReferenceResolution &&
                ReferenceResolution.x > 0.0f && ReferenceResolution.y > 0.0f)
            {
                return ReferenceResolution;
            }
            return Float2{ width, height };
        }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            MeasuredSize = Float2{ constraints.ConstrainWidth(constraints.MaxWidth),
                                   constraints.ConstrainHeight(constraints.MaxHeight) };
            const Float2 inner = LayoutSizeFor(MeasuredSize.x, MeasuredSize.y);
            const BoxConstraints childConstraints = BoxConstraints::Tight(inner.x, inner.y);
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (child->Visibility != VisibilityValue::Gone) { child->Measure(childConstraints); }
            }
        }

        void OnLayout(f32 /*left*/, f32 /*top*/, f32 width, f32 height) override
        {
            const Float2 inner = LayoutSizeFor(width, height);
            f32 scale = 1.0f;
            f32 offsetX = 0.0f;
            f32 offsetY = 0.0f;
            if (inner.x != width || inner.y != height)
            {
                // The standard rule: uniform min-fit, letterboxed and centered.
                scale = Min(width / inner.x, height / inner.y);
                offsetX = (width - inner.x * scale) * 0.5f;
                offsetY = (height - inner.y * scale) * 0.5f;
            }
            for (usize i = 0; i < ChildCount(); ++i)
            {
                View* child = GetChildAt(i);
                if (child->Visibility == VisibilityValue::Gone) { continue; }
                child->Layout(offsetX, offsetY, inner.x, inner.y);
                // Scale around the top-left so drawn rect = offset + scale * content;
                // hit testing undoes the same transform (ViewGroup::HitTest).
                child->Transform.Scale = Float2{ scale, scale };
                child->Transform.Origin = Float2{ 0.0f, 0.0f };
            }
        }
    };

    DRACONIC_DEFINE_OBJECT(CanvasHostView, "draconic::ui")

    // Keep a scene root's canvas hosts sorted by Order, STABLE for ties (the child
    // sequence is component/insertion order between re-sorts). MoveView is a pure
    // reorder (no detach), so focus/hover survive an order change; targets start at
    // child 1 (the billboard layer stays 0) and RootView keeps its popup layer last.
    void SortCanvasHostsByOrder(RootView& root)
    {
        Array<CanvasHostView*> hosts;
        for (usize i = 0; i < root.ChildCount(); ++i)
        {
            if (auto* host = Cast<CanvasHostView>(root.GetChildAt(i))) { hosts.PushBack(host); }
        }
        for (usize i = 1; i < hosts.Size(); ++i)   // stable insertion sort
        {
            CanvasHostView* key = hosts[i];
            usize j = i;
            while (j > 0 && hosts[j - 1]->Order > key->Order) { hosts[j] = hosts[j - 1]; --j; }
            hosts[j] = key;
        }
        for (usize i = 0; i < hosts.Size(); ++i) { root.MoveView(hosts[i], 1 + i); }
    }

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
            UniquePtr<vg::renderer::VGRenderer> renderer;
            u64 begunSerial = 0;   // last UI frame this renderer's ring was reset for
        };
        Array<FormatRenderer> renderers;
        i32 frameCount = 2;

        // RenderTexture canvases: one subsystem-owned offscreen target per RT canvas,
        // keyed by (scene, entity). Reconciled by RenderCanvasTextures each call.
        struct CanvasTarget
        {
            scene::Scene* scene = nullptr;
            scene::EntityHandle entity{};
            rhi::Texture* texture = nullptr;
            rhi::TextureView* view = nullptr;
            u32 width = 0;
            u32 height = 0;
            rhi::ResourceState state = rhi::ResourceState::Undefined;
            bool seen = false;
        };
        Array<CanvasTarget> canvasTargets;

        explicit RenderState(draconic::fonts::IFontService* fonts) : vgContext(fonts) {}

        ~RenderState()
        {
            if (!canvasTargets.IsEmpty() && device != nullptr) { device->WaitIdle(); }
            for (usize i = canvasTargets.Size(); i-- > 0;) { DestroyCanvasTarget(i, false); }
            renderers.Clear();
            if (vertexShader != nullptr && device != nullptr) { device->DestroyShaderModule(vertexShader); }
            if (fragmentShader != nullptr && device != nullptr) { device->DestroyShaderModule(fragmentShader); }
            if (compiler != nullptr) { compiler->Destroy(); }
        }

        // The (created-on-demand) target for one RT canvas, recreated on resize. Null
        // only when texture creation fails.
        [[nodiscard]] CanvasTarget* EnsureCanvasTarget(scene::Scene* scene,
                                                       scene::EntityHandle entity,
                                                       u32 width, u32 height,
                                                       rhi::TextureFormat format)
        {
            CanvasTarget* found = nullptr;
            for (auto& target : canvasTargets)
            {
                if (target.scene == scene && target.entity == entity) { found = &target; break; }
            }
            if (found != nullptr && (found->width != width || found->height != height))
            {
                // Resize: the GPU may still sample the old target - idle before freeing
                // (the ViewportView resize precedent; RT resizes are rare, author-driven).
                device->WaitIdle();
                if (found->view != nullptr) { device->DestroyTextureView(found->view); }
                if (found->texture != nullptr) { device->DestroyTexture(found->texture); }
                found->texture = nullptr;
                found->view = nullptr;
            }
            if (found == nullptr)
            {
                CanvasTarget target;
                target.scene = scene;
                target.entity = entity;
                canvasTargets.PushBack(target);
                found = &canvasTargets[canvasTargets.Size() - 1];
            }
            if (found->texture == nullptr)
            {
                rhi::TextureDesc desc =
                    rhi::TextureDesc::RenderTarget(format, width, height, 1, u8"UICanvasTexture");
                if (!device->CreateTexture(desc, found->texture).IsOk())
                {
                    found->texture = nullptr;
                    return nullptr;
                }
                rhi::TextureViewDesc viewDesc{};
                viewDesc.format = format;
                if (!device->CreateTextureView(found->texture, viewDesc, found->view).IsOk())
                {
                    device->DestroyTexture(found->texture);
                    found->texture = nullptr;
                    found->view = nullptr;
                    return nullptr;
                }
                found->width = width;
                found->height = height;
                found->state = rhi::ResourceState::Undefined;
            }
            return found;
        }

        void DestroyCanvasTarget(usize index, bool waitIdle = true)
        {
            CanvasTarget& target = canvasTargets[index];
            if (device != nullptr)
            {
                if (waitIdle && (target.texture != nullptr || target.view != nullptr))
                {
                    device->WaitIdle();   // the scene may still be sampling it
                }
                if (target.view != nullptr) { device->DestroyTextureView(target.view); }
                if (target.texture != nullptr) { device->DestroyTexture(target.texture); }
            }
            canvasTargets.RemoveAt(index);
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
        [[nodiscard]] vg::renderer::VGRenderer* RendererFor(rhi::TextureFormat format, u64 frameSerial,
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
                auto renderer = MakeUnique<vg::renderer::VGRenderer>(DefaultAllocator());
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
            if (auto* scenes = context->GetSubsystem<scene::SceneSubsystem>())
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
            if (auto* scenes = context->GetSubsystem<scene::SceneSubsystem>())
            {
                scenes->UnregisterSceneAware(this);
            }
        }
        for (SceneUI& ui : m_sceneUIs)
        {
            if (ui.root.Get() != nullptr) { m_context.RemoveRootView(ui.root.Get()); }
        }
        m_sceneUIs.Clear();
        for (TextureCanvasRoot& entry : m_textureCanvasRoots)
        {
            m_context.RemoveRootView(entry.root.Get());
        }
        m_textureCanvasRoots.Clear();
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
        for (TextureCanvasRoot& entry : m_textureCanvasRoots) { entry.seen = false; }
        for (SceneUI& sceneUI : m_sceneUIs)
        {
            scene::Scene* scene = sceneUI.scene;
            auto* canvases = scene->GetSystem<UICanvasComponentManager>();
            if (canvases == nullptr) { continue; }
            // Mark: hosts whose component vanished this frame get swept after the walk
            // (component managers have no destroy hook - despawning a menu entity must
            // still remove its tree from the scene root).
            for (usize i = 0; i < sceneUI.root->ChildCount(); ++i)
            {
                if (auto* host = Cast<CanvasHostView>(sceneUI.root->GetChildAt(i)))
                {
                    host->Seen = false;
                }
            }
            canvases->ForEach([&](UICanvasComponent& c, scene::EntityHandle) {
                const UIDocument* document = c.document.Get();
                const bool wantsTexture = c.renderMode == CanvasRenderMode::RenderTexture;
                const bool builtAsTexture = c.renderRoot.Get() != nullptr;
                if (document != c.builtFrom || (c.root.Get() != nullptr && wantsTexture != builtAsTexture))
                {
                    // Tear down whichever shape was built (document reload or a render-
                    // mode flip), then instantiate for the CURRENT mode.
                    if (c.root.Get() != nullptr)
                    {
                        if (c.host.Get() != nullptr) { c.host->RemoveView(c.root.Get()); }
                        if (c.renderRoot.Get() != nullptr) { c.renderRoot->RemoveView(c.root.Get()); }
                        c.root = nullptr;
                    }
                    if (c.renderRoot.Get() != nullptr)
                    {
                        m_context.RemoveRootView(c.renderRoot.Get());
                        c.renderRoot = nullptr;
                        c.renderTexture = nullptr;       // GPU objects swept by the next
                        c.renderTextureView = nullptr;   // RenderCanvasTextures
                    }
                    if (document != nullptr && !document->markup.IsEmpty())
                    {
                        c.root = MarkupLoader::LoadFromString(document->markup.AsView(), &m_context);
                        if (c.root.Get() == nullptr)
                        {
                            DRACONIC_LOG_WARNING(u8"UI", u8"canvas document failed to instantiate");
                        }
                        else if (wantsTexture)
                        {
                            // RenderTexture: a STANDALONE root - never parented into a
                            // tier (not drawn by the overlay roles) and never an input
                            // root (v1 RT canvases are non-interactive).
                            c.renderRoot = MakeRef<RootView>(DefaultAllocator());
                            c.renderRoot->AddView(c.root.Get());
                            m_context.AddRootView(c.renderRoot.Get());
                            TextureCanvasRoot entry;
                            entry.root = c.renderRoot;
                            m_textureCanvasRoots.PushBack(Move(entry));
                        }
                    }
                    c.builtFrom = document;
                }
                if (c.renderRoot.Get() != nullptr)
                {
                    for (TextureCanvasRoot& entry : m_textureCanvasRoots)
                    {
                        if (entry.root.Get() == c.renderRoot.Get()) { entry.seen = true; break; }
                    }
                }
                if (!wantsTexture)
                {
                    // Overlay canvases parent through their host: the order/scaler
                    // carrier in the scene root. (An unseen host - RT mode or a dead
                    // component - is swept below.)
                    if (c.host.Get() == nullptr)
                    {
                        c.host = MakeRef<CanvasHostView>(DefaultAllocator());
                        sceneUI.root->AddView(c.host.Get());
                    }
                    auto* host = static_cast<CanvasHostView*>(c.host.Get());
                    host->Seen = true;
                    host->Order = c.order;
                    host->ScalerMode = c.scalerMode;
                    host->ReferenceResolution = c.referenceResolution;
                    if (c.root.Get() != nullptr && c.root->Parent == nullptr)
                    {
                        host->AddView(c.root.Get());
                    }
                }
                else if (c.host.Get() != nullptr)
                {
                    c.host = nullptr;   // the stale host stays unseen -> swept below
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
                    // Interactivity gates the SUBTREE; the stretched document root
                    // itself stays hit-TRANSPARENT. The canvas root is the canvas's
                    // screen AREA, not a widget - if it consumed hits, one full-screen
                    // HUD would swallow the pointer everywhere (eating world panels
                    // and gameplay clicks alike). Content that wants a clickable
                    // backdrop uses an explicit full-size child.
                    c.root->IsInteractionEnabled = c.interactive;
                    c.root->IsHitTestVisible = false;
                }
            });
            // Sweep hosts orphaned by component/entity destruction, then keep the
            // canvases stacked by their authored order (billboard layer always below).
            for (usize i = sceneUI.root->ChildCount(); i-- > 0;)
            {
                auto* host = Cast<CanvasHostView>(sceneUI.root->GetChildAt(i));
                if (host != nullptr && !host->Seen) { sceneUI.root->RemoveView(host); }
            }
            SortCanvasHostsByOrder(*sceneUI.root);

            auto* billboards = scene->GetSystem<UIBillboardComponentManager>();
            if (billboards == nullptr) { continue; }
            Array<View*> liveBillboards;   // the sweep below removes everything else
            billboards->ForEach([&](UIBillboardComponent& c, scene::EntityHandle) {
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
                if (c.root.Get() != nullptr) { liveBillboards.PushBack(c.root.Get()); }
            });
            // Sweep nameplates whose component/entity vanished (the canvas hosts'
            // sweep, applied to the billboard layer - managers have no destroy hook).
            for (usize i = sceneUI.billboardLayer->ChildCount(); i-- > 0;)
            {
                View* child = sceneUI.billboardLayer->GetChildAt(i);
                bool live = false;
                for (View* root : liveBillboards) { if (root == child) { live = true; break; } }
                if (!live) { sceneUI.billboardLayer->RemoveView(child); }
            }

            // World panels (the world tier): standalone roots like RT canvases - never
            // parented into a tier, registered through the SAME texture-root registry
            // (its mark-sweep handles despawn); drawn + sprite-driven by
            // RenderCanvasTextures; the pump ray-routes the pointer into them.
            if (auto* panels = scene->GetSystem<UIWorldPanelComponentManager>())
            {
                panels->ForEach([&](UIWorldPanelComponent& c, scene::EntityHandle) {
                    const UIDocument* document = c.document.Get();
                    if (document != c.builtFrom)
                    {
                        if (c.renderRoot.Get() != nullptr)
                        {
                            m_context.RemoveRootView(c.renderRoot.Get());
                            c.renderRoot = nullptr;
                            c.root = nullptr;
                            c.renderTexture = nullptr;       // swept by RenderCanvasTextures
                            c.renderTextureView = nullptr;
                        }
                        if (document != nullptr && !document->markup.IsEmpty())
                        {
                            c.root = MarkupLoader::LoadFromString(document->markup.AsView(),
                                                                  &m_context);
                            if (c.root.Get() != nullptr)
                            {
                                c.renderRoot = MakeRef<RootView>(DefaultAllocator());
                                c.renderRoot->AddView(c.root.Get());
                                m_context.AddRootView(c.renderRoot.Get());
                                TextureCanvasRoot entry;
                                entry.root = c.renderRoot;
                                m_textureCanvasRoots.PushBack(Move(entry));
                            }
                            else
                            {
                                DRACONIC_LOG_WARNING(u8"UI",
                                    u8"world panel document failed to instantiate");
                            }
                        }
                        c.builtFrom = document;
                    }
                    if (c.renderRoot.Get() != nullptr)
                    {
                        for (TextureCanvasRoot& entry : m_textureCanvasRoots)
                        {
                            if (entry.root.Get() == c.renderRoot.Get())
                            {
                                entry.seen = true;
                                break;
                            }
                        }
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
                });
            }
        }
        // Sweep RenderTexture roots whose component vanished (despawn/removal/scene
        // teardown): unregister from the context - which stores roots NON-OWNING, so a
        // stale registration would dangle - then drop our keep-alive ref. (A mode-flip/
        // rebuild teardown above already unregistered; the second remove is a no-op.)
        for (usize i = m_textureCanvasRoots.Size(); i-- > 0;)
        {
            if (m_textureCanvasRoots[i].seen) { continue; }
            m_context.RemoveRootView(m_textureCanvasRoots[i].root.Get());
            m_textureCanvasRoots.RemoveAt(i);
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

        // Per-surface scene binding (game-ui.md §9 known edge): which scene roots may
        // this frame's input reach? A BOUND source (the editor's Game tab binds its
        // scene on Play) confines routing + consumption to ITS scene's root - two
        // interactive scenes visible at once can no longer cross-route on overlapping
        // coordinates. An UN-BOUND source follows the input subsystem's policy:
        // AllScenes in the player (the shell owns the whole window - the historical
        // behavior), ScreenTierOnly in the editor's embedded runtime, where editing-
        // page HUDs render WYSIWYG but are deliberately NOT interactive (Simulate
        // included - the Game tab is the interactive-run surface). The scene-LESS
        // screen tier is never confined: global overlays sit above every scene and
        // stay modal while occupied.
        const void* boundSceneKey = m_input->BoundSceneKey();
        const bool unboundReachesScenes = m_input->UnboundScenePolicy() ==
                                          draconic::input::UnboundInputScenePolicy::AllScenes;
        auto sceneRootEligible = [&](const SceneUI& ui) noexcept {
            if (boundSceneKey != nullptr)
            {
                return static_cast<const void*>(ui.scene) == boundSceneKey;
            }
            return unboundReachesScenes;
        };

        // World-panel pointer routing state: when the ray hits a panel, its root
        // becomes the active input root and the DISPATCH coordinates become the
        // panel-local pixels (the root's own space) instead of the raw pointer.
        bool panelPointer = false;
        Float2 panelPointerPx{ 0.0f, 0.0f };

        // The context dispatches input through ONE ActiveInputRoot (the UIHost multi-
        // window precedent) - with the tiers split across roots, pick it per frame:
        // an OCCUPIED screen tier is modal and always wins; otherwise the ELIGIBLE
        // root under the pointer; otherwise the nearest INTERACTIVE WORLD PANEL under
        // the camera ray; otherwise (pad/keyboard-only) the first eligible scene root
        // with canvas content, so gamepad nav reaches a pause menu no pointer hovered.
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
                    if (!sceneRootEligible(m_sceneUIs[i])) { continue; }
                    RootView* root = m_sceneUIs[i].root.Get();
                    if (root == nullptr) { continue; }
                    View* hit = root->HitTest(point);
                    if (hit != nullptr && hit != root) { target = root; }
                }
                // World tier: the pointer missed every overlay - cast the scene
                // camera's ray and take the NEAREST interactive panel it crosses.
                // In CAPTURED/look mode (FPS flying) the cursor is parked wherever it
                // was grabbed - the crosshair IS the pointer, so the ray goes through
                // the view center instead.
                if (target == nullptr)
                {
                    const bool centerAim = mouse->RelativeMode();
                    const bool rayDebug = std::getenv("DRACONIC_UI_RAY_DEBUG") != nullptr;
                    f32 bestDistance = 0.0f;
                    for (SceneUI& sceneUI : m_sceneUIs)
                    {
                        if (!sceneRootEligible(sceneUI)) { continue; }
                        auto* panels =
                            sceneUI.scene->GetSystem<UIWorldPanelComponentManager>();
                        if (panels == nullptr) { continue; }
                        // The surface's content size = the scene root's last-drawn
                        // viewport (zero before the first frame renders - no ray yet).
                        const Float2 viewSize = sceneUI.root.Get() != nullptr
                            ? sceneUI.root->ViewportSize : Float2{ 0.0f, 0.0f };
                        if (viewSize.x <= 0.0f || viewSize.y <= 0.0f) { continue; }
                        draconic::render::ViewCamera camera;
                        if (!draconic::render::ExtractPrimaryCamera(*sceneUI.scene, camera))
                        {
                            continue;
                        }
                        const Float2 rayPoint = centerAim
                            ? Float2{ viewSize.x * 0.5f, viewSize.y * 0.5f } : point;
                        Float3 rayOrigin, rayDirection;
                        PointerRayFromCamera(camera, rayPoint, viewSize, rayOrigin, rayDirection);
                        if (rayDebug)
                        {
                            std::fprintf(stderr,
                                "[UIRAY] view=%.0fx%.0f point=(%.0f,%.0f) center=%d origin=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f)\n",
                                viewSize.x, viewSize.y, rayPoint.x, rayPoint.y,
                                centerAim ? 1 : 0, rayOrigin.x, rayOrigin.y, rayOrigin.z,
                                rayDirection.x, rayDirection.y, rayDirection.z);
                        }
                        panels->ForEach([&](UIWorldPanelComponent& c,
                                            scene::EntityHandle e) {
                            if (!c.interactive || !c.visible) { return; }
                            if (c.renderRoot.Get() == nullptr) { return; }
                            const WorldPanelHit hit = RayHitWorldPanel(
                                rayOrigin, rayDirection,
                                sceneUI.scene->GetWorldMatrix(e), c.sizeMeters);
                            if (rayDebug)
                            {
                                const Float4x4 w = sceneUI.scene->GetWorldMatrix(e);
                                std::fprintf(stderr,
                                    "[UIRAY]   panel at (%.2f,%.2f,%.2f) size=(%.1f,%.1f) hit=%d uv=(%.2f,%.2f) d=%.2f\n",
                                    w.m[3][0], w.m[3][1], w.m[3][2], c.sizeMeters.x,
                                    c.sizeMeters.y, hit.hit ? 1 : 0, hit.uv.x, hit.uv.y,
                                    hit.distance);
                            }
                            if (!hit.hit) { return; }
                            if (target != nullptr && hit.distance >= bestDistance) { return; }
                            target = c.renderRoot.Get();
                            bestDistance = hit.distance;
                            const Float2 rootSize = c.renderRoot->ViewportSize;
                            panelPointerPx = Float2{ hit.uv.x * rootSize.x,
                                                     hit.uv.y * rootSize.y };
                            panelPointer = true;
                        });
                    }
                    if (target == nullptr) { panelPointer = false; }
                }
            }
            if (target == nullptr)
            {
                for (SceneUI& ui : m_sceneUIs)
                {
                    if (!sceneRootEligible(ui)) { continue; }
                    // Beyond the billboard layer = at least one canvas instantiated.
                    if (ui.root.Get() != nullptr && ui.root->ChildCount() > 1)
                    {
                        target = ui.root.Get();
                        break;
                    }
                }
            }
            if (std::getenv("DRACONIC_UI_RAY_DEBUG") != nullptr)
            {
                const char* kind = "none";
                if (target == m_screenRoot.Get()) { kind = overlayActive ? "screen(modal)" : "screen(hit)"; }
                else if (target != nullptr)
                {
                    kind = "scene-or-panel";
                    for (SceneUI& ui : m_sceneUIs)
                    {
                        if (target == ui.root.Get()) { kind = "scene-root"; break; }
                    }
                }
                std::fprintf(stderr, "[UIRAY] frame target=%s mouse=%d pos=(%.0f,%.0f)\n",
                             kind, mouse != nullptr ? 1 : 0,
                             mouse != nullptr ? mouse->X() : -1.0f,
                             mouse != nullptr ? mouse->Y() : -1.0f);
            }
            if (target == nullptr) { target = m_screenRoot.Get(); }
            if (target != nullptr) { m_context.SetActiveInputRoot(target); }
        }
        if (mouse != nullptr)
        {
            // Panel routing swaps in panel-local pixels: the active root IS the panel's
            // standalone root, so dispatch coordinates live in its texture space.
            const f32 x = panelPointer ? panelPointerPx.x : mouse->X();
            const f32 y = panelPointer ? panelPointerPx.y : mouse->Y();
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

        // ---- keyboard + text (game-ui.md P3): the tagged event stream off the SAME
        // provider seam - ordered key events and TextInput payloads that polling cannot
        // carry. The bridge applies the standard shell->UI key mapping (Return stays
        // dispatch-first) and reconciles the IME after every event; mouse/pad kinds are
        // skipped here (mouse is polled above - dispatching both would double-fire).
        // The provider gates: the player streams its window's events, the Game tab's
        // viewport source streams only while the viewport owns keyboard focus. ----
        for (const draconic::shell::InputEvent& event : devices.Events())
        {
            switch (event.kind)
            {
            case draconic::shell::InputEventKind::KeyDown:
            case draconic::shell::InputEventKind::KeyUp:
            case draconic::shell::InputEventKind::TextInput:
                (void)m_bridge.Dispatch(event);
                break;
            default:
                break;
            }
        }
        // Reconcile the IME even on event-less frames: focus can move without a key or
        // click (gamepad navigation onto/off an EditText).
        m_bridge.SyncTextInput();

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
        // screen tier first (topmost), then the ELIGIBLE scene roots - the same
        // binding rule as routing, so an un-bound editor context never publishes a
        // spurious mask from HUDs it cannot interact with.
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
                if (!sceneRootEligible(m_sceneUIs[i])) { continue; }
                RootView* root = m_sceneUIs[i].root.Get();
                if (root == nullptr) { continue; }
                View* hit = root->HitTest(point);
                pointer = hit != nullptr && hit != root;
            }
        }
        // A ray-hit interactive panel consumes the pointer like any hovered canvas.
        pointer = pointer || panelPointer || inputManager.PressedId() != ViewId{};
        const bool keyboard = m_context.WantsTextInput();
        m_pointerConsumed = pointer;
        m_input->Runtime().SetConsumptionMask(
            draconic::input::ActionRuntime::ConsumptionMask{ pointer, keyboard });
    }

    // The scene-tier per-view sync: canvas visibility from the authored flag, then
    // billboard projection through the VIEW's real camera (world -> clip -> NDC -> px;
    // behind-camera parks at (-10000,-10000); distance scale as a 2D view-transform).
    // Public + encoder-free so headless tests drive it with a synthetic view.
    void UISubsystem::UpdateSceneView(scene::Scene& scene, const render::SceneOverlayView& view)
    {
        if (auto* canvases = scene.GetSystem<UICanvasComponentManager>())
        {
            canvases->ForEach([&](UICanvasComponent& c, scene::EntityHandle) {
                if (c.root.Get() != nullptr)
                {
                    c.root->Visibility = c.visible ? VisibilityValue::Visible
                                                   : VisibilityValue::Gone;
                }
            });
        }
        if (auto* billboards = scene.GetSystem<UIBillboardComponentManager>())
        {
            billboards->ForEach([&](UIBillboardComponent& c, scene::EntityHandle e) {
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
                    // Pixels in VIEWPORT space: the scene root lays out at the viewport
                    // size and the VG viewport seam places it at the view's rect.
                    const f32 ndcX = clip.x / clip.w;
                    const f32 ndcY = clip.y / clip.w;
                    lp->X = (ndcX * 0.5f + 0.5f) * static_cast<f32>(view.viewportWidth);
                    lp->Y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<f32>(view.viewportHeight);
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
        if (view.viewportWidth == 0 || view.viewportHeight == 0) { return; }
        SceneUI* sceneUI = nullptr;
        for (SceneUI& ui : m_sceneUIs)
        {
            if (static_cast<const void*>(ui.scene) == view.sceneKey) { sceneUI = &ui; break; }
        }
        if (sceneUI == nullptr || sceneUI->root.Get() == nullptr) { return; }
        // The scene root lays out at the VIEWPORT size and draws at the view's rect
        // (split-screen halves each lay out their own HUD, clipped to their half).
        UpdateSceneView(*sceneUI->scene, view);
        DrawRootInPass(*sceneUI->root, encoder, view.targetFormat, view.viewportX, view.viewportY,
                       view.viewportWidth, view.viewportHeight, static_cast<i32>(view.frameIndex));
    }

    // Screen tier (IScreenOverlay): called from the host's RenderOverlays per window
    // target, after the scene composed. Draws the scene-less global overlays.
    void UISubsystem::Render(rhi::RenderPassEncoder& encoder, const render::ScreenOverlayView& view)
    {
        if (m_render.Get() == nullptr || m_render->device == nullptr) { return; }
        if (m_screenRoot.Get() == nullptr || view.width == 0 || view.height == 0) { return; }
        DrawRootInPass(*m_screenRoot, encoder, view.targetFormat, 0, 0, view.width, view.height,
                       static_cast<i32>(view.frameIndex));
    }

    // Records one root into an ALREADY-ACTIVE render pass: layout at the CONTENT size
    // (width/height), batch through the shared VGContext, upload a slice (pure mapped-
    // memory writes - legal during pass recording), draw at (viewportX, viewportY) via
    // the VG viewport seam. The per-format renderer's ring resets once per UI frame
    // (m_frameSerial) so same-frame draws never clobber each other.
    void UISubsystem::DrawRootInPass(RootView& root, rhi::RenderPassEncoder& encoder,
                                     rhi::TextureFormat format, i32 viewportX, i32 viewportY,
                                     u32 width, u32 height, i32 frameIndex)
    {
        root.ViewportSize = Float2{ static_cast<f32>(width), static_cast<f32>(height) };
        m_context.UpdateRootView(&root);

        m_render->vgContext.Clear();
        m_context.DrawRootView(&root, m_render->vgContext);
        vg::VGBatch& batch = m_render->vgContext.GetBatch();
        if (batch.commands.IsEmpty()) { return; }

        vg::renderer::VGRenderer* renderer = m_render->RendererFor(format, m_frameSerial, frameIndex);
        if (renderer == nullptr) { return; }
        const vg::renderer::VGRenderSlice slice = renderer->Prepare(batch, frameIndex, width, height);
        renderer->Render(encoder, viewportX, viewportY, width, height, frameIndex, slice);
    }

    // RenderTexture canvases (game-ui.md P3): draw each RT canvas's standalone root into
    // its subsystem-owned offscreen texture. Runs on the HOST's encoder BEFORE the scene
    // render (the RenderCanvasTextures seam next to EnsureRenderReady/RenderOverlays),
    // so materials sampling the texture see this frame's UI. Targets are created and
    // resized on demand, swept when their canvas vanishes or leaves the mode, and end
    // in ShaderRead. The VG ring gating is the shared one: DrawRootInPass goes through
    // RendererFor(format, m_frameSerial, frameIndex), which resets a format renderer's
    // ring at most once per UI frame - same-frame overlay draws are never clobbered.
    void UISubsystem::RenderCanvasTextures(rhi::CommandEncoder& encoder, i32 frameIndex)
    {
        // sRGB so the stored encoding matches the swapchain path: the VG shader emits
        // linear, the hardware encodes on write and decodes on sample - the panel's
        // sprite feeds the SAME linear values into the scene the HUD feeds the window.
        // (A world panel still tone-maps with the scene afterwards - it's IN the world;
        // that residual difference vs the post-tonemap HUD is by design.)
        constexpr rhi::TextureFormat kCanvasTextureFormat = rhi::TextureFormat::RGBA8UnormSrgb;
        if (m_render.Get() == nullptr || m_render->device == nullptr) { return; }
        // At most ONCE per UI frame: several hosts share one runtime context in the
        // editor (the Game tab + every open scene page call this seam), and one call
        // already renders EVERY scene's RT canvases - repeat calls would draw the same
        // targets again on the same encoder.
        if (m_canvasTexturesSerial == m_frameSerial) { return; }
        m_canvasTexturesSerial = m_frameSerial;
        for (auto& target : m_render->canvasTargets) { target.seen = false; }
        for (SceneUI& sceneUI : m_sceneUIs)
        {
            auto* canvases = sceneUI.scene->GetSystem<UICanvasComponentManager>();
            if (canvases == nullptr) { continue; }
            auto* sprites = sceneUI.scene->GetSystem<draconic::render::SpriteComponentManager>();
            auto* decals = sceneUI.scene->GetSystem<draconic::render::DecalComponentManager>();
            // Declarative RT-canvas -> material binding: the canvas ENTITY's own sprite/
            // decal runtime `texture` override tracks the canvas's CURRENT view (which
            // changes on resize). Deliberately UI-side: it writes the SAME override
            // manual/script assignment uses, so the render subsystem stays UI-unaware -
            // no new render fields, no importer/inspector surface. Cross-entity binding
            // stays manual (script refreshes from CanvasRenderTextureView per frame).
            auto bindEntityMaterials = [&](scene::EntityHandle entity, rhi::TextureView* oldView,
                                           rhi::TextureView* newView) {
                if (auto* sprite = sprites != nullptr ? sprites->Get(entity) : nullptr)
                {
                    if (newView != nullptr || sprite->texture == oldView) { sprite->texture = newView; }
                }
                if (auto* decal = decals != nullptr ? decals->Get(entity) : nullptr)
                {
                    if (newView != nullptr || decal->texture == oldView) { decal->texture = newView; }
                }
            };
            canvases->ForEach([&](UICanvasComponent& c, scene::EntityHandle entity) {
                if (c.renderMode != CanvasRenderMode::RenderTexture) { return; }
                if (c.renderRoot.Get() == nullptr) { return; }   // no document instantiated
                const u32 width = Max(c.renderTextureWidth, 1u);
                const u32 height = Max(c.renderTextureHeight, 1u);
                rhi::TextureView* previousView = c.renderTextureView;
                RenderState::CanvasTarget* target = m_render->EnsureCanvasTarget(
                    sceneUI.scene, entity, width, height, kCanvasTextureFormat);
                if (target == nullptr)
                {
                    c.renderTexture = nullptr;
                    c.renderTextureView = nullptr;
                    bindEntityMaterials(entity, previousView, nullptr);   // never leave a freed view bound
                    return;
                }
                target->seen = true;
                c.renderTexture = target->texture;      // the component-level accessor
                c.renderTextureView = target->view;
                bindEntityMaterials(entity, previousView, target->view);
                if (!c.visible) { return; }   // keep the texture, skip the draw
                encoder.TransitionTexture(target->texture, target->state,
                                          rhi::ResourceState::RenderTarget);
                rhi::RenderPassDesc pass;
                rhi::ColorAttachment color;
                color.view = target->view;
                color.loadOp = rhi::LoadOp::Clear;   // fresh transparent background
                color.storeOp = rhi::StoreOp::Store;
                color.clearValue = rhi::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f };
                pass.colorAttachments.Add(color);
                if (rhi::RenderPassEncoder* rp = encoder.BeginRenderPass(pass))
                {
                    DrawRootInPass(*c.renderRoot, *rp, kCanvasTextureFormat, 0, 0,
                                   width, height, frameIndex);
                    rp->End();
                }
                encoder.TransitionTexture(target->texture, rhi::ResourceState::RenderTarget,
                                          rhi::ResourceState::ShaderRead);
                target->state = rhi::ResourceState::ShaderRead;
            });

            // World panels: same target machinery, sized by PIXELS-PER-METER, and the
            // sibling sprite is DRIVEN outright (EntityOriented + sizeMeters + texture)
            // - the panel IS the authoring surface, the sprite is its render vehicle.
            // One RT consumer per entity: a panel and an RT canvas on the same entity
            // would collide on the (scene, entity) target key - warned, panel wins.
            if (auto* panels = sceneUI.scene->GetSystem<UIWorldPanelComponentManager>())
            {
                panels->ForEach([&](UIWorldPanelComponent& c, scene::EntityHandle entity) {
                    if (c.renderRoot.Get() == nullptr) { return; }
                    const f32 ppm = Max(c.pixelsPerMeter, 1.0f);
                    const u32 width = Clamp<u32>(
                        static_cast<u32>(c.sizeMeters.x * ppm + 0.5f), 16u, 2048u);
                    const u32 height = Clamp<u32>(
                        static_cast<u32>(c.sizeMeters.y * ppm + 0.5f), 16u, 2048u);
                    RenderState::CanvasTarget* target = m_render->EnsureCanvasTarget(
                        sceneUI.scene, entity, width, height, kCanvasTextureFormat);
                    if (target == nullptr)
                    {
                        c.renderTexture = nullptr;
                        c.renderTextureView = nullptr;
                        return;
                    }
                    target->seen = true;
                    c.renderTexture = target->texture;
                    c.renderTextureView = target->view;

                    // Drive the sprite (auto-added): the panel's quad in the world.
                    if (sprites != nullptr)
                    {
                        draconic::render::SpriteComponent* sprite = sprites->Get(entity);
                        if (sprite == nullptr) { sprite = &sprites->Add(entity); }
                        sprite->orientation = draconic::render::SpriteOrientation::EntityOriented;
                        sprite->size = c.sizeMeters;
                        sprite->texture = target->view;
                        sprite->visible = c.visible;
                        // Post-tonemap: the panel keeps its AUTHORED colors (matching
                        // the screen-tier HUD) while still depth-testing into the scene.
                        sprite->postTonemap = true;
                    }
                    if (!c.visible) { return; }   // keep the texture, skip the draw
                    encoder.TransitionTexture(target->texture, target->state,
                                              rhi::ResourceState::RenderTarget);
                    rhi::RenderPassDesc pass;
                    rhi::ColorAttachment color;
                    color.view = target->view;
                    color.loadOp = rhi::LoadOp::Clear;
                    color.storeOp = rhi::StoreOp::Store;
                    color.clearValue = rhi::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f };
                    pass.colorAttachments.Add(color);
                    if (rhi::RenderPassEncoder* rp = encoder.BeginRenderPass(pass))
                    {
                        DrawRootInPass(*c.renderRoot, *rp, kCanvasTextureFormat, 0, 0,
                                       width, height, frameIndex);
                        rp->End();
                    }
                    encoder.TransitionTexture(target->texture, rhi::ResourceState::RenderTarget,
                                              rhi::ResourceState::ShaderRead);
                    target->state = rhi::ResourceState::ShaderRead;
                });
            }
        }
        // Sweep targets whose canvas vanished (despawn, scene destroyed, mode flip) -
        // and un-bind any sprite/decal override still pointing at the dying view (only
        // while the scene itself is alive; a destroyed scene took its components along).
        for (usize i = m_render->canvasTargets.Size(); i-- > 0;)
        {
            RenderState::CanvasTarget& target = m_render->canvasTargets[i];
            if (target.seen) { continue; }
            bool sceneAlive = false;   // pointer compare only - the scene may be freed
            for (const SceneUI& ui : m_sceneUIs)
            {
                if (ui.scene == target.scene) { sceneAlive = true; break; }
            }
            if (target.view != nullptr && sceneAlive)
            {
                if (auto* sprites = target.scene->GetSystem<draconic::render::SpriteComponentManager>())
                {
                    if (auto* sprite = sprites->Get(target.entity);
                        sprite != nullptr && sprite->texture == target.view)
                    {
                        sprite->texture = nullptr;
                    }
                }
                if (auto* decals = target.scene->GetSystem<draconic::render::DecalComponentManager>())
                {
                    if (auto* decal = decals->Get(target.entity);
                        decal != nullptr && decal->texture == target.view)
                    {
                        decal->texture = nullptr;
                    }
                }
            }
            m_render->DestroyCanvasTarget(i);
        }
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
            DrawRootInPass(root, *rp, format, 0, 0, width, height, frameIndex);
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

    void UISubsystem::SetDefaultTheme(const UITheme* theme)
    {
        RefPtr<StyleSheet> sheet;
        if (theme != nullptr && !theme->stylesheet.IsEmpty())
        {
            StyleSheetLoader loader;
            loader.SetPalette(GameTheme::Palette());
            sheet = loader.Load(theme->stylesheet.AsView());
            if (sheet.Get() == nullptr)
            {
                DRACONIC_LOG_WARNING(u8"UI",
                    u8"default UI theme failed to parse - keeping the built-in GameTheme");
            }
        }
        m_theme = sheet.Get() != nullptr ? sheet : GameTheme::Create();
        m_context.SetStyleSheet(m_theme);
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
        (void)m_render->CompileOne(vg::renderer::VertexShaderSource(), draconic::shaders::ShaderStage::Vertex,
                                   u8"gameui.vg.vert", m_render->vertexShader);
        (void)m_render->CompileOne(vg::renderer::FragmentShaderSource(), draconic::shaders::ShaderStage::Fragment,
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

    DRACONIC_REFLECT_ENUM(CanvasRenderMode, "draconic::ui")
    {
        builder.Value("ScreenOverlay", CanvasRenderMode::ScreenOverlay);
        builder.Value("RenderTexture", CanvasRenderMode::RenderTexture);
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
        builder.DataVersion(2);   // v2 added the RenderTexture canvas mode
        builder.Property<&UICanvasComponent::document>("document");
        builder.Property<&UICanvasComponent::theme>("theme");
        builder.Property<&UICanvasComponent::order>("order");
        builder.Property<&UICanvasComponent::visible>("visible");
        builder.Property<&UICanvasComponent::interactive>("interactive");
        builder.Property<&UICanvasComponent::scalerMode>("scalerMode");
        builder.Property<&UICanvasComponent::referenceResolution>("referenceResolution");
        builder.Property<&UICanvasComponent::renderMode>("renderMode");
        builder.Property<&UICanvasComponent::renderTextureWidth>("renderTextureWidth");
        builder.Property<&UICanvasComponent::renderTextureHeight>("renderTextureHeight");
    }

    DRACONIC_REFLECT_VALUE(UIWorldPanelComponent, "draconic::ui")
    {
        builder.DataVersion(1);
        builder.Property<&UIWorldPanelComponent::document>("document");
        builder.Property<&UIWorldPanelComponent::theme>("theme");
        builder.Property<&UIWorldPanelComponent::sizeMeters>("sizeMeters");
        builder.Property<&UIWorldPanelComponent::pixelsPerMeter>("pixelsPerMeter");
        builder.Property<&UIWorldPanelComponent::interactive>("interactive");
        builder.Property<&UIWorldPanelComponent::visible>("visible");
    }

    void RegisterUIComponentReflection()
    {
        static const bool once = []() {
            DraconicRegisterEnum_CanvasScalerMode();
            DraconicRegisterEnum_CanvasRenderMode();
            DraconicRegisterEnum_BillboardOrientation();
            DraconicRegisterEnum_BillboardScale();
            DraconicRegisterValue_UICanvasComponent();
            DraconicRegisterValue_UIWorldPanelComponent();
            DraconicRegisterValue_UIBillboardComponent();
            return true;
        }();
        (void)once;
    }
}
