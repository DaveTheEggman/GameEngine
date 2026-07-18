// Draconic::UISubsystem - the `draconic.ui.subsystem` module (game-ui.md P1).
//
// The game screen tier: UICanvasComponents reference cooked UIDocuments; the subsystem
// owns ONE UIContext (GameTheme default stylesheet; core controls only - never the
// toolkit), instantiates a fresh view tree per canvas, lays out against the render
// target, draws through the VG renderer into an overlay pass AFTER the scene (the
// player's backbuffer and the editor Game tab's viewport texture alike), and feeds UI
// input from the SAME device facades the action layer reads - publishing the
// pointer/keyboard consumption mask so UI-consumed input never reaches gameplay
// actions (raw facades stay unfiltered). UI ticks on UNSCALED time (menus animate
// while the game is paused), which is why the work runs in the BeginFrame lane.
//
// GCC modules hygiene: render/VG/shader contact + the DRACONIC_REFLECT_* bodies live in
// UISubsystemImpl.cpp (implementation unit), same split as physics.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui.subsystem;

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
import draconic.ui;
import draconic.ui.resource;

using namespace draconic::core;

export namespace draconic::ui
{
    namespace dscene = draconic::scene;

    enum class CanvasScalerMode : u8
    {
        ConstantPixel = 0,     // 1 UI px = 1 target px
        ReferenceResolution,   // uniform-scale so referenceResolution fits the target
    };

    // A screen-space UI canvas on an entity: menus/HUD ride in scenes and prefabs
    // (spawn/despawn = open/close). renderMode is implicitly ScreenOverlay in P1.
    struct UICanvasComponent
    {
        // Authored:
        draconic::resource::Ref<UIDocument> document;
        draconic::resource::Ref<UITheme> theme;    // optional override (nil = context theme)
        i32 order = 0;                             // draw/dispatch order (higher = on top)
        bool visible = true;
        bool interactive = true;
        CanvasScalerMode scalerMode = CanvasScalerMode::ConstantPixel;
        Float2 referenceResolution{ 1920.0f, 1080.0f };

        // Runtime (transient):
        RefPtr<View> root;                         // instantiated tree (template = document)
        const UIDocument* builtFrom = nullptr;     // rebuild detector (hot reload)
        RefPtr<StyleSheet> themeSheet;             // parsed override (built on theme change)
        const UITheme* themeFrom = nullptr;
    };

    inline void Serialize(ISerializer& ar, UICanvasComponent& c)
    {
        draconic::core::Serialize(ar, "document", c.document);
        draconic::core::Serialize(ar, "theme", c.theme);
        draconic::core::Serialize(ar, "order", c.order);
        draconic::core::Serialize(ar, "visible", c.visible);
        u8 scaler = static_cast<u8>(c.scalerMode);
        draconic::core::Serialize(ar, "scalerMode", scaler);
        c.scalerMode = static_cast<CanvasScalerMode>(scaler);
        draconic::core::Serialize(ar, "referenceResolution", c.referenceResolution);
        draconic::core::Serialize(ar, "interactive", c.interactive);
    }

    inline void ResolveResources(draconic::resource::ResourceManager& manager, UICanvasComponent& c)
    {
        c.document.Bind(manager);
        c.theme.Bind(manager);
    }

    class UICanvasComponentManager final
        : public dscene::SerializableComponentManager<UICanvasComponent>
    {
    public:
        UICanvasComponentManager()
            : SerializableComponentManager<UICanvasComponent>(u8"ui.Canvas") {}
    };

    // ---- billboards (P2: nameplates/health bars) - the Sedulous reference's
    // best-behaved tier, ported as-is: ONE shared layer under the canvases, one VG
    // batch; world position -> clip -> screen px; behind-camera anchors park off-screen
    // (clipped + unhit, no tree churn); distance scaling as a 2D view-transform. ----

    enum class BillboardOrientation : u8
    {
        Screen = 0,     // offset in ENTITY-LOCAL space (rides the entity's rotation)
        Cylindrical,    // offset in WORLD space (a fixed lift above the anchor)
    };
    enum class BillboardScale : u8 { Fixed = 0, Distance };

    struct UIBillboardComponent
    {
        // Authored:
        draconic::resource::Ref<UIDocument> document;
        Float3 offset{ 0.0f, 0.0f, 0.0f };
        BillboardOrientation orientation = BillboardOrientation::Cylindrical;
        BillboardScale scaleMode = BillboardScale::Fixed;
        f32 referenceDistance = 10.0f;   // Distance mode: scale = clamp(ref/dist, min, max)
        f32 minScale = 0.3f;
        f32 maxScale = 2.0f;
        bool visible = true;

        // Runtime (transient):
        RefPtr<View> root;
        const UIDocument* builtFrom = nullptr;
    };

    inline void Serialize(ISerializer& ar, UIBillboardComponent& c)
    {
        draconic::core::Serialize(ar, "document", c.document);
        draconic::core::Serialize(ar, "offset", c.offset);
        u8 orientation = static_cast<u8>(c.orientation);
        draconic::core::Serialize(ar, "orientation", orientation);
        c.orientation = static_cast<BillboardOrientation>(orientation);
        u8 scale = static_cast<u8>(c.scaleMode);
        draconic::core::Serialize(ar, "scaleMode", scale);
        c.scaleMode = static_cast<BillboardScale>(scale);
        draconic::core::Serialize(ar, "referenceDistance", c.referenceDistance);
        draconic::core::Serialize(ar, "minScale", c.minScale);
        draconic::core::Serialize(ar, "maxScale", c.maxScale);
        draconic::core::Serialize(ar, "visible", c.visible);
    }

    inline void ResolveResources(draconic::resource::ResourceManager& manager, UIBillboardComponent& c)
    {
        c.document.Bind(manager);
    }

    class UIBillboardComponentManager final
        : public dscene::SerializableComponentManager<UIBillboardComponent>
    {
    public:
        UIBillboardComponentManager()
            : SerializableComponentManager<UIBillboardComponent>(u8"ui.Billboard") {}
    };

    void RegisterUIComponentReflection();

    class UISubsystem final : public draconic::runtime::Subsystem,
                              public dscene::ISceneAware
    {
    public:
        UISubsystem();   // defined in the impl unit (RenderState is opaque here)
        ~UISubsystem() override;   // defined in the impl unit (RenderState is opaque here)

        /// Before the scene subsystem so canvas visibility/trees are current for pages;
        /// lane choice matters more than order: ALL work runs in BeginFrame (raw dt).
        [[nodiscard]] i32 UpdateOrder() const noexcept override { return -650; }

        /// Optional TTF for the default font ("" = try the repo-relative Roboto, else
        /// text simply doesn't render). Preset before Startup.
        void SetFontPath(StringView path) { m_fontPath = String(path); }

        [[nodiscard]] UIContext& Context() noexcept { return m_context; }
        [[nodiscard]] RootView* ScreenRoot() noexcept { return m_screenRoot.Get(); }

        // ---- the scene-less SCREEN tier (Sedulous ScreenUIView) ----
        // Global overlays OUTSIDE any scene: they survive scene swaps (loading screens,
        // system menus) and draw ABOVE every scene's canvases in every target. Pushed
        // from code; the caller keeps the returned/passed view to remove it later.

        /// Instantiates `document` and attaches it topmost. Null if the markup fails.
        RefPtr<View> PushScreenOverlay(const UIDocument& document)
        {
            if (document.markup.IsEmpty() || m_overlayLayer.Get() == nullptr) { return {}; }
            RefPtr<View> view = MarkupLoader::LoadFromString(document.markup.AsView(), &m_context);
            if (view.Get() != nullptr) { m_overlayLayer->AddView(view.Get()); }
            return view;
        }
        /// Attaches an already-built view topmost (code-built overlays).
        void PushScreenOverlay(RefPtr<View> view)
        {
            if (view.Get() != nullptr && m_overlayLayer.Get() != nullptr)
            {
                m_overlayLayer->AddView(view.Get());
            }
        }
        void RemoveScreenOverlay(View* view)
        {
            if (view != nullptr && m_overlayLayer.Get() != nullptr)
            {
                m_overlayLayer->RemoveView(view);
            }
        }
        [[nodiscard]] usize ScreenOverlayCount() const noexcept
        {
            return m_overlayLayer.Get() != nullptr ? m_overlayLayer->ChildCount() : 0;
        }

        // ---- lifecycle (definitions in UISubsystemImpl.cpp) ----
        void OnInit() override;
        void OnShutdown() override;
        void OnReady() override;
        void BeginFrame(f32 deltaTime) override;

        void OnSceneCreated(dscene::Scene& scene) override
        {
            scene.AddSystem<UICanvasComponentManager>();
            scene.AddSystem<UIBillboardComponentManager>();
            m_scenes.PushBack(&scene);
        }
        void OnSceneDestroyed(dscene::Scene& scene) override
        {
            for (usize i = 0; i < m_scenes.Size(); ++i)
            {
                if (m_scenes[i] == &scene) { m_scenes.RemoveAt(i); break; }
            }
        }

        /// Draws `scene`'s screen tier (its canvases + billboards ONLY - a Simulate
        /// page's UI never bleeds into the Game tab) into `target` via a Load-op pass on
        /// the SAME encoder, AFTER the scene composed (post-EndRendering). Billboards
        /// project through the scene's primary camera. The target must be in
        /// RenderTarget state; it is left there. Lays out against the live target size.
        void RenderOverlay(dscene::Scene& scene, rhi::CommandEncoder& encoder,
                           rhi::TextureView* target, rhi::TextureFormat format,
                           u32 width, u32 height, i32 frameIndex);

        /// One-time GPU bring-up (shader compile + device wire) by whoever owns graphics
        /// (DefaultApplication's startup). Idempotent; without it RenderOverlay no-ops.
        void EnsureRenderReady(rhi::Device& device, i32 frameCount);

        // ---- editor preview seam (the UIDocumentPage) ----
        // Previews render through THIS context - the GAME's fonts, theme, style
        // resolution, and VG path - into a DEDICATED RootView that is never attached to
        // the screen root (it cannot leak into game targets) and receives no input
        // (view-only; the ActiveInputRoot stays the screen root).

        /// Instantiates `document` into a fresh preview root. Null on parse failure.
        [[nodiscard]] RefPtr<RootView> CreatePreview(const UIDocument& document);
        void DestroyPreview(RootView* root);
        /// Draws a preview root into `target` via a Load-op pass on the caller's encoder
        /// (target in RenderTarget state; left there). Lays out at the given size.
        void RenderPreview(RootView& root, rhi::CommandEncoder& encoder,
                           rhi::TextureView* target, rhi::TextureFormat format,
                           u32 width, u32 height, i32 frameIndex);

        /// True when any interactive canvas is under the pointer or holds text focus -
        /// mirrors the published consumption mask (tests + gameplay diagnostics).
        [[nodiscard]] bool PointerOverUI() const noexcept { return m_pointerConsumed; }

    private:
        void SyncCanvases();
        void PumpInput();
        void DrawRootInto(RootView& root, rhi::CommandEncoder& encoder, rhi::TextureView* target,
                          rhi::TextureFormat format, u32 width, u32 height, i32 frameIndex);

        String m_fontPath;
        UIContext m_context;
        RefPtr<RootView> m_screenRoot;
        RefPtr<ViewGroup> m_billboardLayer;   // shared, BELOW the canvases; one batch
        RefPtr<ViewGroup> m_overlayLayer;     // scene-LESS screen tier, ABOVE everything
        RefPtr<StyleSheet> m_theme;
        UniquePtr<draconic::fonts::TrueTypeFontService> m_fonts;
        Array<dscene::Scene*> m_scenes;
        draconic::input::InputSubsystem* m_input = nullptr;

        // pointer edge tracking for the polled pump
        bool m_prevButtons[3] = { false, false, false };
        f32 m_prevWheel = 0.0f;
        bool m_pointerConsumed = false;

        // gamepad focus navigation (hold-repeat per direction)
        f32 m_navRepeat[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // Up/Down/Left/Right
        bool m_navHeld[4] = { false, false, false, false };
        f32 m_navDeltaTime = 0.0f;

        // impl-side render state (VG contexts/renderers/shaders), opaque here
        struct RenderState;
        UniquePtr<RenderState> m_render;
    };
}
