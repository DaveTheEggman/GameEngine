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
// stays lit), and the Radius field tracks the tool live (the mouse wheel resizes the brush, and the
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

        RefPtr<ui::View> MakeRow(StringView title, f32 fontSize)
        {
            auto lbl = MakeRef<ui::Label>(DefaultAllocator(), title);
            lbl->FontSize.SetValue(Optional<f32>{fontSize});
            return lbl;
        }

        RefPtr<ui::FlexLayout> MakePanelRoot()
        {
            auto root = MakeRef<ui::FlexLayout>(DefaultAllocator());
            root->Direction = ui::Orientation::Vertical;
            root->Spacing = 4.0f;
            return root;
        }

        // A segmented row of exclusive toggle buttons - exactly one lit. contentFor(i) supplies each
        // button's content (icon or label); onSelect(i) applies the choice to the tool; current()
        // returns the selected index for the lit state. Re-lights via SetSilent + Invalidate (no
        // OnCheckedChanged recursion). The buttons capture `this` (this row owns them, so it outlives
        // their closures); the move-only callbacks live as members (never copied into the closures).
        class SegmentedToggle final : public ui::FlexLayout
        {
        public:
            SegmentedToggle()
            {
                Direction = ui::Orientation::Horizontal;
                Spacing = 3.0f;
            }

            void Build(i32 count, Function<RefPtr<ui::View>(i32)> contentFor,
                       Function<void(i32)> onSelect, Function<i32()> current,
                       Function<StringView(i32)> tooltipFor = {})
            {
                m_onSelect = Move(onSelect);
                m_current = Move(current);
                for (i32 i = 0; i < count; ++i)
                {
                    auto btn = MakeRef<ui::ToggleButton>(DefaultAllocator());
                    btn->SetContent(contentFor(i));
                    if (tooltipFor)
                    {
                        btn->TooltipText = String(tooltipFor(i));
                    }
                    SegmentedToggle* self = this;
                    btn->OnCheckedChanged.Add(
                        [self, i](ui::ToggleButton*, bool) { self->Choose(i); });
                    AddView(btn.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));
                }
                Refresh();
            }

            void Choose(i32 i)
            {
                if (m_onSelect)
                {
                    m_onSelect(i);
                }
                Refresh();
            }
            void Refresh()
            {
                const i32 sel = m_current ? m_current() : -1;
                for (usize k = 0; k < ChildCount(); ++k)
                {
                    if (auto* tb = Cast<ui::ToggleButton>(GetChildAt(k)))
                    {
                        tb->IsChecked.SetSilent(static_cast<i32>(k) == sel);
                        tb->Invalidate();
                    }
                }
            }

        private:
            Function<void(i32)> m_onSelect;
            Function<i32()> m_current;
        };

        RefPtr<ui::toolkit::FloatEditor> AddFloat(ui::toolkit::PropertyGrid& grid, StringView label,
                                                  f64 value, f64 lo, f64 hi, f64 step, i32 decimals,
                                                  Function<void(f64)> onChange)
        {
            auto fe = MakeRef<ui::toolkit::FloatEditor>(DefaultAllocator(), label, value, lo, hi, step,
                                                        decimals, Move(onChange));
            grid.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(fe.Get()));
            return fe;
        }

        // The property grid fills the panel body so it resizes with the FloatingPanel.
        void AddGrid(ui::FlexLayout& root, RefPtr<ui::toolkit::PropertyGrid> grid)
        {
            auto glp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            glp->Width = ui::SizeSpec::Match();
            glp->Grow = 1.0f;
            root.AddView(grid.Get(), glp);
        }

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

                auto root = MakePanelRoot();
                root->AddView(MakeRow(u8"Sculpt mode", 12.0f).Get(),
                              MakeRef<ui::LayoutParams>(DefaultAllocator()));

                auto modes = MakeRef<SegmentedToggle>(DefaultAllocator());
                modes->Build(
                    4,
                    [modeIcons](i32 i) -> RefPtr<ui::View> {
                        return RefPtr<ui::View>(
                            MakeRef<IconGlyph>(DefaultAllocator(), modeIcons[i], 16.0f).Get());
                    },
                    [t](i32 i) { t->SetMode(static_cast<TerrainSculptTool::Mode>(i)); },
                    [t]() { return static_cast<i32>(t->GetMode()); },
                    [](i32 i) -> StringView {
                        static constexpr StringView kNames[4] = {u8"Raise", u8"Lower", u8"Smooth",
                                                                 u8"Flatten"};
                        return kNames[i];
                    });
                root->AddView(modes.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(DefaultAllocator());
                RefPtr<ui::toolkit::FloatEditor> radiusFe =
                    AddFloat(*grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                             [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                AddFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 50.0, 0.5, 1,
                         [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
                // The wheel resizes the brush; mirror it into the field. SetValue is edit-guarded, so
                // this won't re-fire the setter. The RefPtr keeps the field alive for the callback.
                t->OnRadiusChanged = [radiusFe](f32 r) { radiusFe->SetValue(static_cast<f64>(r)); };

                AddGrid(*root, grid);
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

                auto root = MakePanelRoot();
                if (thumbs != nullptr && ls.count > 0)
                {
                    root->AddView(MakeRow(u8"Base (erase to reveal)", 11.0f).Get(),
                                  MakeRef<ui::LayoutParams>(DefaultAllocator()));
                    auto baseSwatch = MakeRef<LayerSwatch>(DefaultAllocator(), thumbs, ls.baseId,
                                                           fallbackIcon, 24.0f);
                    baseSwatch->TooltipText = SwatchAssetName(ectx, ls.baseId);
                    root->AddView(baseSwatch.Get(),
                                  MakeRef<ui::LayoutParams>(DefaultAllocator()));
                }
                root->AddView(MakeRow(u8"Paint layer", 12.0f).Get(),
                              MakeRef<ui::LayoutParams>(DefaultAllocator()));

                // Slots 0..N-1 = palette layers; slot N = the eraser. The palette is UNBOUNDED:
                // the resolved terrain's real count always wins (numbered labels when thumbnails
                // are unavailable); 4 numbered slots only when no terrain resolved at all.
                const i32 paletteCount = ls.count > 0 ? static_cast<i32>(ls.count) : 4;
                auto layers = MakeRef<SegmentedToggle>(DefaultAllocator());
                layers->Build(
                    paletteCount + 1,
                    [useThumbs, thumbs, ls, fallbackIcon, paletteCount](i32 i) -> RefPtr<ui::View> {
                        if (i == paletteCount)
                        {
                            auto lbl = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"E"));
                            lbl->FontSize.SetValue(Optional<f32>{12.0f});
                            return RefPtr<ui::View>(lbl.Get());
                        }
                        if (useThumbs)
                        {
                            return RefPtr<ui::View>(MakeRef<LayerSwatch>(DefaultAllocator(), thumbs,
                                                                        ls.ids[i], fallbackIcon, 24.0f)
                                                        .Get());
                        }
                        const String num = Format(u8"{}", i);
                        auto lbl = MakeRef<ui::Label>(DefaultAllocator(), num.AsView());
                        lbl->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(lbl.Get());
                    },
                    [t, paletteCount](i32 i)
                    {
                        if (i == paletteCount)
                        {
                            t->SetEraser(true);
                        }
                        else
                        {
                            t->SetPaletteIndex(static_cast<u32>(i));
                        }
                    },
                    [t, paletteCount]()
                    {
                        return t->IsEraser() ? paletteCount
                                             : static_cast<i32>(t->PaletteIndex());
                    },
                    [paletteCount, names = Move(layerNames)](i32 i) -> StringView {
                        if (i == paletteCount)
                        {
                            return u8"Eraser (reveals base)";
                        }
                        return (i >= 0 && static_cast<usize>(i) < names.Size())
                                   ? names[static_cast<usize>(i)].AsView()
                                   : StringView(u8"Paint layer");
                    });
                root->AddView(layers.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(DefaultAllocator());
                RefPtr<ui::toolkit::FloatEditor> radiusFe =
                    AddFloat(*grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                             [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                AddFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 1.0, 0.05, 2,
                         [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
                // Spacing = stamp density along the stroke (fraction of the radius): low + low
                // strength = smooth soft blending; high = discrete dabs.
                AddFloat(*grid, u8"Spacing", static_cast<f64>(t->Spacing()), 0.05, 1.0, 0.05, 2,
                         [t](f64 v) { t->SetSpacing(static_cast<f32>(v)); });
                // Airbrush = time-cadence stamps while HOLDING (build-up by hovering).
                {
                    auto ab = MakeRef<ui::toolkit::BoolEditor>(
                        DefaultAllocator(), StringView(u8"Airbrush"), t->IsAirbrush(),
                        Function<void(bool)>{[t](bool v) { t->SetAirbrush(v); }});
                    grid->AddProperty(RefPtr<ui::toolkit::PropertyEditor>(ab.Get()));
                }
                t->OnRadiusChanged = [radiusFe](f32 r) { radiusFe->SetValue(static_cast<f64>(r)); };

                AddGrid(*root, grid);
                return root;
            }
        };
    }

    void RegisterTerrainToolPanels()
    {
        static SculptPanelProvider sculptPanel;
        static SplatPanelProvider splatPanel;
        ViewportToolPanelRegistry::Get().Register(&sculptPanel);
        ViewportToolPanelRegistry::Get().Register(&splatPanel);
    }
}
