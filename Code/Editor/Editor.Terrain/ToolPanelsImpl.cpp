// Editor::Terrain - tool-panel providers (the FIRST consumers of the parked viewport-tool-panel
// seam, editor.app:tool_panel). A provider builds the on-screen settings panel for a terrain brush
// tool; the scene page's ViewportToolPanelHost mounts it in the bottom dock while that tool is active.
//
// UI lives HERE (ui.toolkit), out of the UI-free :sculpt/:splat tool partitions. Each provider
// downcasts the active IViewportTool to its concrete tool (the id matched, so the type is known -
// the framework contract) to read/write the brush params the UI-free tool cannot expose.

module;
#include "Core/Prelude.h"

module editor.terrain;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import editor.app;          // IViewportToolPanelProvider, ViewportToolPanelRegistry
import editor.viewporttools; // IViewportTool + ViewportToolHostContext

using namespace foundation::core;

namespace editor
{
    namespace ui = foundation::ui;

    namespace
    {
        // Shared builders (compact vertical panel: a title, a row of choice buttons, a float grid).
        RefPtr<ui::View> MakeRow(StringView title, f32 fontSize)
        {
            auto lbl = MakeRef<ui::Label>(DefaultAllocator(), title);
            lbl->FontSize.SetValue(Optional<f32>{fontSize});
            return lbl;
        }

        RefPtr<ui::FlexLayout> MakeButtonRow()
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            return row;
        }

        void AddButton(ui::FlexLayout& row, StringView label, Function<void()> onClick)
        {
            auto btn = MakeRef<ui::Button>(DefaultAllocator(), label);
            btn->FontSize.SetValue(Optional<f32>{11.0f});
            btn->OnClick.Add([cb = Move(onClick)](ui::ButtonBase*) { cb(); });
            row.AddView(btn.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));
        }

        void AddFloat(ui::toolkit::PropertyGrid& grid, StringView label, f64 value, f64 lo, f64 hi,
                      f64 step, i32 decimals, Function<void(f64)> onChange)
        {
            auto fe = MakeRef<ui::toolkit::FloatEditor>(DefaultAllocator(), label, value, lo, hi, step,
                                                        decimals, Move(onChange));
            grid.AddProperty(RefPtr<ui::toolkit::PropertyEditor>(fe.Get()));
        }

        RefPtr<ui::FlexLayout> MakePanelRoot()
        {
            auto root = MakeRef<ui::FlexLayout>(DefaultAllocator());
            root->Direction = ui::Orientation::Vertical;
            return root;
        }

        // ---- Sculpt panel: mode buttons + radius + strength ------------------------------------
        class SculptPanelProvider final : public IViewportToolPanelProvider
        {
        public:
            [[nodiscard]] StringView ToolId() const override { return u8"terrain.sculpt"; }
            // Brush settings as a draggable panel over the viewport (clamped to it). Flip to
            // ViewportOverlay (fixed corner HUD) or Dock (bottom tab) to compare.
            [[nodiscard]] ToolPanelPlacement Placement() const override
            {
                return ToolPanelPlacement::Float;
            }

            [[nodiscard]] RefPtr<ui::View> CreatePanel(IViewportTool& tool,
                                                       const ViewportToolHostContext&) override
            {
                TerrainSculptTool* t = static_cast<TerrainSculptTool*>(&tool); // id matched -> type known
                auto root = MakePanelRoot();
                root->AddView(MakeRow(u8"Sculpt mode", 12.0f).Get(),
                              MakeRef<ui::LayoutParams>(DefaultAllocator()));
                auto modes = MakeButtonRow();
                AddButton(*modes, u8"Raise", [t]() { t->SetMode(TerrainSculptTool::Mode::Raise); });
                AddButton(*modes, u8"Lower", [t]() { t->SetMode(TerrainSculptTool::Mode::Lower); });
                AddButton(*modes, u8"Smooth", [t]() { t->SetMode(TerrainSculptTool::Mode::Smooth); });
                AddButton(*modes, u8"Flatten", [t]() { t->SetMode(TerrainSculptTool::Mode::Flatten); });
                root->AddView(modes.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(DefaultAllocator());
                AddFloat(*grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                         [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                AddFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 50.0, 0.5, 1,
                         [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
                {
                    // Bound the grid to its two rows: its internal ScrollView otherwise fills all
                    // available space (both axes), which stretched the panel over the whole viewport.
                    auto glp = MakeRef<ui::LayoutParams>(DefaultAllocator());
                    glp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(200.0f));  // don't let the grid demand full width
                    glp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(64.0f)); // radius + strength rows
                    root->AddView(grid.Get(), glp);
                }
                return root;
            }
        };

        // ---- Splat panel: layer buttons + radius + strength ------------------------------------
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
                auto root = MakePanelRoot();
                root->AddView(MakeRow(u8"Paint layer", 12.0f).Get(),
                              MakeRef<ui::LayoutParams>(DefaultAllocator()));
                auto layers = MakeButtonRow();
                AddButton(*layers, u8"0", [t]() { t->SetLayer(0); });
                AddButton(*layers, u8"1", [t]() { t->SetLayer(1); });
                AddButton(*layers, u8"2", [t]() { t->SetLayer(2); });
                AddButton(*layers, u8"3", [t]() { t->SetLayer(3); });
                root->AddView(layers.Get(), MakeRef<ui::LayoutParams>(DefaultAllocator()));

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(DefaultAllocator());
                AddFloat(*grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                         [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                AddFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 1.0, 0.05, 2,
                         [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
                {
                    // Bound the grid to its two rows: its internal ScrollView otherwise fills all
                    // available space (both axes), which stretched the panel over the whole viewport.
                    auto glp = MakeRef<ui::LayoutParams>(DefaultAllocator());
                    glp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(200.0f));  // don't let the grid demand full width
                    glp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(64.0f)); // radius + strength rows
                    root->AddView(grid.Get(), glp);
                }
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
