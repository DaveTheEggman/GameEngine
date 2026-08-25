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
import editor.app;          // IViewportToolPanelProvider, ViewportToolPanelRegistry, EditorIcons
import editor.viewporttools; // IViewportTool + ViewportToolHostContext

using namespace foundation::core;

namespace editor
{
    namespace ui = foundation::ui;

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
                       Function<void(i32)> onSelect, Function<i32()> current)
            {
                m_onSelect = Move(onSelect);
                m_current = Move(current);
                for (i32 i = 0; i < count; ++i)
                {
                    auto btn = MakeRef<ui::ToggleButton>(DefaultAllocator());
                    btn->SetContent(contentFor(i));
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
                    [t]() { return static_cast<i32>(t->GetMode()); });
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
                                                       const ViewportToolHostContext&) override
            {
                TerrainSplatTool* t = static_cast<TerrainSplatTool*>(&tool);
                static constexpr StringView kLayerLabels[4] = {u8"0", u8"1", u8"2", u8"3"};

                auto root = MakePanelRoot();
                root->AddView(MakeRow(u8"Paint layer", 12.0f).Get(),
                              MakeRef<ui::LayoutParams>(DefaultAllocator()));

                auto layers = MakeRef<SegmentedToggle>(DefaultAllocator());
                layers->Build(
                    4,
                    [](i32 i) -> RefPtr<ui::View> {
                        auto lbl = MakeRef<ui::Label>(DefaultAllocator(), kLayerLabels[i]);
                        lbl->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(lbl.Get());
                    },
                    [t](i32 i) { t->SetLayer(static_cast<u32>(i)); },
                    [t]() { return static_cast<i32>(t->Layer()); });
                root->AddView(layers.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(DefaultAllocator());
                RefPtr<ui::toolkit::FloatEditor> radiusFe =
                    AddFloat(*grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                             [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                AddFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 1.0, 0.05, 2,
                         [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
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
