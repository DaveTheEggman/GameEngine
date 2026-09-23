// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Terrain - tool-panel providers (the FIRST consumers of the viewport-tool-panel seam,
// editor.app:tool_panel). A provider builds the on-screen settings panel for a terrain brush tool;
// the scene page's ViewportToolPanelHost mounts it in the active tool's FloatingPanel.
//
// UI lives HERE (ui.toolkit), out of the UI-free :sculpt/:splat tool partitions. Each provider
// downcasts the active IViewportTool to its concrete tool (the id matched, so the type is known -
// the framework contract) to read/write the brush params the UI-free tool cannot expose.
//
// UX: the mode / layer choices are a segmented row of exclusive TOGGLE icon buttons (the active one
// stays lit), and the Radius field tracks the tool live (Shift + wheel resizes the brush, and the
// tool's OnRadiusChanged pushes the new value back into the field).

module;
#include "Core/Prelude.h"

module editor.terrain;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.scene;    // Scene, ComponentManagerBase, EntityHandle
import foundation.content;  // Instance::Name (swatch name tooltips)
import engine.terrain;      // TerrainComponent(Manager)
import foundation.terrain;  // TerrainResource + Layer (albedo Ref)
import editor.core;          // EditorContext, ThumbnailService (splat layer thumbnails)
import editor.app;          // IViewportToolPanelProvider, ViewportToolPanelRegistry, EditorIcons
import editor.viewporttools; // IViewportTool + ViewportToolHostContext

using namespace foundation::core;

namespace editor
{
    namespace ui = foundation::ui;
    namespace scene = foundation::scene;

    namespace
    {
        // A tintable SVG glyph as a leaf view (for icon content inside a ToggleButton). ImageView is
        // raster-only, so this small view renders a shared BakedSVGDrawable at its bounds.
        class IconGlyph final : public ui::View
        {
        public:
            IconGlyph(ui::SVGDrawable* icon, f32 size) : m_icon(icon), m_size(size) {}

        protected:
            void OnMeasure(ui::BoxConstraints c) override
            {
                MeasuredSize = Float2{c.ConstrainWidth(m_size), c.ConstrainHeight(m_size)};
            }
            void OnDraw(ui::UIDrawContext& ctx) override
            {
                if (m_icon == nullptr)
                {
                    return;
                }
                const Color tint =
                    ResolveStyleColor(ui::StyleProperty::TextColor, Color{0.88f, 0.9f, 0.94f, 1.0f});
                const Optional<Color> prev = m_icon->TintColor;
                m_icon->TintColor = Optional<Color>(tint);
                m_icon->Draw(ctx, Rectangle{0, 0, Width(), Height()});
                m_icon->TintColor = prev;
            }

        private:
            ui::SVGDrawable* m_icon; // borrowed (owned by EditorIcons; outlives the panel)
            f32 m_size;
        };

        // A live texture-thumbnail swatch for a terrain layer albedo. ThumbnailService::Get is async
        // (empty now, fills in later), so OnDraw re-queries every frame (cheap - a cache hit or a
        // cached negative) and falls back to the texture asset icon until the thumbnail is ready.
        class LayerSwatch final : public ui::View
        {
        public:
            LayerSwatch(ThumbnailService* thumbs, Guid albedoId, ui::Drawable* fallback, f32 size)
                : m_thumbs(thumbs), m_id(albedoId), m_fallback(fallback), m_size(size)
            {
            }

        protected:
            void OnMeasure(ui::BoxConstraints c) override
            {
                MeasuredSize = Float2{c.ConstrainWidth(m_size), c.ConstrainHeight(m_size)};
            }
            void OnDraw(ui::UIDrawContext& ctx) override
            {
                RefPtr<ui::Drawable> thumb = (m_thumbs != nullptr && !m_id.IsNil())
                                                 ? m_thumbs->Get(m_id)
                                                 : RefPtr<ui::Drawable>{};
                ui::Drawable* d = thumb ? thumb.Get() : m_fallback;
                if (d != nullptr)
                {
                    d->Draw(ctx, Rectangle{0, 0, Width(), Height()});
                }
            }

        private:
            ThumbnailService* m_thumbs; // borrowed (app-owned)
            Guid m_id;
            ui::Drawable* m_fallback;   // borrowed (EditorIcons texture icon)
            f32 m_size;
        };

        // The layers driving the splat picker: albedo Guids of the FIRST terrain in the scene. The
        // tool paints whichever terrain is under the cursor; the panel scopes its labels to the
        // primary terrain (the common one-terrain case). count is the terrain's real LayerCount.
        struct LayerSet
        {
            u32 count = 0;   // palette layer count (unbounded)
            Guid baseId;     // the BASE layer's albedo asset (separate, never painted)
            Array<Guid> ids; // palette albedo asset guids
        };
        // The albedo asset's NAME for a swatch tooltip - grayscale maps (a displacement picked in
        // place of its diffuse sibling) are indistinguishable at swatch size, so the name is the
        // disambiguator (the ImportTest/TerrainProject white-paint hunt).
        String SwatchAssetName(EditorContext* ctx, const Guid& id)
        {
            if (id.IsNil())
            {
                return String(u8"(no albedo)");
            }
            if (ctx != nullptr && ctx->Project() != nullptr)
            {
                if (auto* inst = ctx->Project()->SourceDb().GetInstance(id))
                {
                    return String(inst->Name());
                }
            }
            return String(u8"(missing)");
        }

        LayerSet ResolveLayers(scene::Scene& sc)
        {
            LayerSet out;
            scene::ComponentManagerBase* base =
                sc.FindManagerByComponentType(TypeOf<engine::terrain::TerrainComponent>());
            auto* mgr = static_cast<engine::terrain::TerrainComponentManager*>(base);
            if (mgr == nullptr)
            {
                return out;
            }
            mgr->ForEach(
                [&](engine::terrain::TerrainComponent& c, scene::EntityHandle)
                {
                    if (out.count > 0)
                    {
                        return; // first terrain with layers wins
                    }
                    foundation::terrain::TerrainResource* res = c.terrain.Get();
                    if (res == nullptr)
                    {
                        return;
                    }
                    out.baseId = res->base.albedo.id;
                    const u32 n = res->PaletteCount();
                    for (u32 i = 0; i < n; ++i)
                    {
                        out.ids.PushBack(res->palette[i].albedo.id);
                    }
                    out.count = n;
                });
            return out;
        }

        // The panel widget kit lives in editor.app:tool_panel_widgets (shared with the
        // vegetation brush); the terrain-only pieces above (swatches, the layer set) stay here.
        using app::SegmentedToggle;
        using app::MakeToolPanelRoot;
        using app::MakeToolPanelRow;
        using app::AddToolPanelFloat;
        using app::AddToolPanelGrid;

        // ---- Sculpt panel: mode icon toggles + radius + strength ---------------------------------
        class SculptPanelProvider final : public IViewportToolPanelProvider
        {
        public:
            [[nodiscard]] StringView ToolId() const override { return u8"terrain.sculpt"; }
            [[nodiscard]] ToolPanelPlacement Placement() const override
            {
                return ToolPanelPlacement::Float;
            }

            [[nodiscard]] RefPtr<ui::View> CreatePanel(IViewportTool& tool,
                                                       const ViewportToolHostContext&) override
            {
                TerrainSculptTool* t = static_cast<TerrainSculptTool*>(&tool); // id matched -> type known
                app::EditorIcons& icons = app::EditorIcons::Get();
                ui::SVGDrawable* modeIcons[4] = {icons.brushRaise.Get(), icons.brushLower.Get(),
                                                 icons.brushSmooth.Get(), icons.brushFlatten.Get()};

                auto root = MakeToolPanelRoot();
                root->AddView(MakeToolPanelRow(u8"Sculpt mode", 12.0f).Get());

                auto modes = MakeRef<SegmentedToggle>(editor::EditorRootAllocator());
                modes->Build(
                    4,
                    [modeIcons](i32 i) -> RefPtr<ui::View> {
                        return RefPtr<ui::View>(
                            MakeRef<IconGlyph>(editor::EditorRootAllocator(), modeIcons[i], 16.0f).Get());
                    },
                    [t](i32 i) { t->SetMode(static_cast<TerrainSculptTool::Mode>(i)); },
                    [t]() { return static_cast<i32>(t->GetMode()); },
                    [](i32 i) -> StringView {
                        static constexpr StringView kNames[4] = {u8"Raise", u8"Lower", u8"Smooth",
                                                                 u8"Flatten"};
                        return kNames[i];
                    });
                root->AddView(modes.Get());

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(editor::EditorRootAllocator());
                RefPtr<ui::toolkit::FloatEditor> radiusFe =
                    AddToolPanelFloat(*grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                             [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                AddToolPanelFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 50.0, 0.5, 1,
                         [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
                // Shift + wheel resizes the brush; mirror it into the field. SetValue is edit-guarded, so
                // this won't re-fire the setter. The RefPtr keeps the field alive for the callback.
                t->OnRadiusChanged = [radiusFe](f32 r) { radiusFe->SetValue(static_cast<f64>(r)); };

                AddToolPanelGrid(*root, grid);
                return root;
            }
        };

        // ---- Hole panel: Cut / Fill + radius (Specs/terrain-holes.md) ----------------------------
        class HolePanelProvider final : public IViewportToolPanelProvider
        {
        public:
            [[nodiscard]] StringView ToolId() const override { return u8"terrain.hole"; }
            [[nodiscard]] ToolPanelPlacement Placement() const override
            {
                return ToolPanelPlacement::Float;
            }

            [[nodiscard]] RefPtr<ui::View> CreatePanel(IViewportTool& tool,
                                                       const ViewportToolHostContext&) override
            {
                TerrainHoleTool* t = static_cast<TerrainHoleTool*>(&tool); // id matched -> type known
                auto root = MakeToolPanelRoot();
                root->AddView(MakeToolPanelRow(u8"Mode", 12.0f).Get());
                auto modes = MakeRef<SegmentedToggle>(editor::EditorRootAllocator());
                modes->Build(
                    2,
                    [](i32 i) -> RefPtr<ui::View>
                    {
                        const StringView text = i == 0 ? StringView(u8"Cut") : StringView(u8"Fill");
                        auto label = MakeRef<ui::Label>(editor::EditorRootAllocator(), text);
                        label->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(label.Get());
                    },
                    [t](i32 i) { t->SetMode(i == 0 ? TerrainHoleTool::Mode::Cut : TerrainHoleTool::Mode::Fill); },
                    [t]() -> i32 { return t->GetMode() == TerrainHoleTool::Mode::Cut ? 0 : 1; },
                    [](i32 i) -> StringView
                    {
                        return i == 0 ? StringView(u8"Cut the surface under the brush: nothing draws, "
                                                   u8"collides, walks or grows there")
                                      : StringView(u8"Fill a cut back in");
                    });
                root->AddView(modes.Get());
                auto grid = MakeRef<ui::toolkit::PropertyGrid>(editor::EditorRootAllocator());
                RefPtr<ui::toolkit::FloatEditor> radiusFe =
                    AddToolPanelFloat(*grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                                      [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                t->OnRadiusChanged = [radiusFe](f32 r) { radiusFe->SetValue(static_cast<f64>(r)); };
                AddToolPanelGrid(*root, grid);
                return root;
            }
        };

        // ---- Splat panel: layer toggles + radius + strength --------------------------------------
        class SplatPanelProvider final : public IViewportToolPanelProvider
        {
        public:
            [[nodiscard]] StringView ToolId() const override { return u8"terrain.splat"; }
            [[nodiscard]] ToolPanelPlacement Placement() const override
            {
                return ToolPanelPlacement::Float;
            }

            [[nodiscard]] RefPtr<ui::View> CreatePanel(IViewportTool& tool,
                                                       const ViewportToolHostContext& ctx) override
            {
                TerrainSplatTool* t = static_cast<TerrainSplatTool*>(&tool);
                ThumbnailService* thumbs =
                    (ctx.editorContext != nullptr) ? ctx.editorContext->Thumbnails() : nullptr;
                const LayerSet ls = (ctx.scene != nullptr) ? ResolveLayers(*ctx.scene) : LayerSet{};
                // The BASE swatch shows separately (labeled, NOT selectable - the base is never
                // painted; erase reveals it). The palette row is the paint choices + the ERASER as
                // the last slot (top-K model). Thumbnails when resolvable; numbered fallback.
                const bool useThumbs = ls.count > 0 && thumbs != nullptr;
                ui::Drawable* fallbackIcon = app::EditorIcons::Get().texture.Get();

                // Name tooltips per swatch (thumbnails alone cannot tell a displacement map from
                // its diffuse sibling); owned Strings, copied into TooltipText at Build.
                EditorContext* ectx = ctx.editorContext;
                Array<String> layerNames;
                for (u32 li = 0; li < ls.count; ++li)
                {
                    layerNames.PushBack(SwatchAssetName(ectx, ls.ids[li]));
                }

                auto root = MakeToolPanelRoot();
                if (thumbs != nullptr && ls.count > 0)
                {
                    root->AddView(MakeToolPanelRow(u8"Base (erase to reveal)", 11.0f).Get());
                    auto baseSwatch = MakeRef<LayerSwatch>(editor::EditorRootAllocator(), thumbs, ls.baseId,
                                                           fallbackIcon, 24.0f);
                    baseSwatch->TooltipText = SwatchAssetName(ectx, ls.baseId);
                    root->AddView(baseSwatch.Get());
                }
                root->AddView(MakeToolPanelRow(u8"Paint layer", 12.0f).Get());

                // Slots 0..N-1 = palette layers; slot N = the eraser; slot N+1 = smooth (blur).
                // The palette is UNBOUNDED: the resolved terrain's real count always wins
                // (numbered labels when thumbnails are unavailable); 4 numbered slots only when
                // no terrain resolved at all.
                const i32 paletteCount = ls.count > 0 ? static_cast<i32>(ls.count) : 4;
                auto layers = MakeRef<SegmentedToggle>(editor::EditorRootAllocator());
                layers->Build(
                    paletteCount + 2,
                    [useThumbs, thumbs, ls, fallbackIcon, paletteCount](i32 i) -> RefPtr<ui::View> {
                        if (i >= paletteCount)
                        {
                            const StringView text =
                                i == paletteCount ? StringView(u8"E") : StringView(u8"S");
                            auto lbl = MakeRef<ui::Label>(editor::EditorRootAllocator(), text);
                            lbl->FontSize.SetValue(Optional<f32>{12.0f});
                            return RefPtr<ui::View>(lbl.Get());
                        }
                        if (useThumbs)
                        {
                            return RefPtr<ui::View>(MakeRef<LayerSwatch>(editor::EditorRootAllocator(), thumbs,
                                                                        ls.ids[i], fallbackIcon, 24.0f)
                                                        .Get());
                        }
                        const String num = Format(u8"{}", i);
                        auto lbl = MakeRef<ui::Label>(editor::EditorRootAllocator(), num.AsView());
                        lbl->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(lbl.Get());
                    },
                    [t, paletteCount](i32 i)
                    {
                        if (i == paletteCount)
                        {
                            t->SetEraser(true);
                        }
                        else if (i == paletteCount + 1)
                        {
                            t->SetSmooth(true);
                        }
                        else
                        {
                            t->SetPaletteIndex(static_cast<u32>(i));
                        }
                    },
                    [t, paletteCount]()
                    {
                        return t->IsEraser()   ? paletteCount
                               : t->IsSmooth() ? paletteCount + 1
                                               : static_cast<i32>(t->PaletteIndex());
                    },
                    [paletteCount, names = Move(layerNames)](i32 i) -> StringView {
                        if (i == paletteCount)
                        {
                            return u8"Eraser (reveals base)";
                        }
                        if (i == paletteCount + 1)
                        {
                            return u8"Smooth (feathers painted seams)";
                        }
                        return (i >= 0 && static_cast<usize>(i) < names.Size())
                                   ? names[static_cast<usize>(i)].AsView()
                                   : StringView(u8"Paint layer");
                    });
                root->AddView(layers.Get());

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(editor::EditorRootAllocator());
                RefPtr<ui::toolkit::FloatEditor> radiusFe =
                    AddToolPanelFloat(*grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                             [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                AddToolPanelFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 1.0, 0.05, 2,
                         [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
                // Spacing = stamp density along the stroke (fraction of the radius): low + low
                // strength = smooth soft blending; high = discrete dabs.
                AddToolPanelFloat(*grid, u8"Stamp spacing", static_cast<f64>(t->Spacing()), 0.05, 1.0, 0.05, 2,
                         [t](f64 v) { t->SetSpacing(static_cast<f32>(v)); });
                // Airbrush = time-cadence stamps while HOLDING (build-up by hovering).
                {
                    auto ab = MakeRef<ui::toolkit::BoolEditor>(
                        editor::EditorRootAllocator(), StringView(u8"Airbrush"), t->IsAirbrush(),
                        Function<void(bool)>{[t](bool v) { t->SetAirbrush(v); }});
                    grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(ab.Get()));
                }
                t->OnRadiusChanged = [radiusFe](f32 r) { radiusFe->SetValue(static_cast<f64>(r)); };

                AddToolPanelGrid(*root, grid);
                return root;
            }
        };
    }

    void RegisterTerrainToolPanels()
    {
        static SculptPanelProvider sculptPanel;
        static SplatPanelProvider splatPanel;
        static HolePanelProvider holePanel;
        ViewportToolPanelRegistry::Get().Register(&sculptPanel);
        ViewportToolPanelRegistry::Get().Register(&splatPanel);
        ViewportToolPanelRegistry::Get().Register(&holePanel);
    }
}
