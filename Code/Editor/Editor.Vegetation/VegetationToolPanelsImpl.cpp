// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Vegetation - the Paint Vegetation brush's settings panel (the viewport-tool-panel seam,
// editor.app:tool_panel), composed from the shared panel widget kit: a plane toggle row (planes
// 0..N-1 of the scene's first vegetation mask, then E = eraser, S = smooth), radius, strength,
// spacing and airbrush - the splat panel's layout, so the two footprint brushes read the same.

module;
#include "Core/Prelude.h"

module editor.vegetation;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.scene;               // Scene, ComponentManagerBase, EntityHandle
import foundation.vegetation.resource; // VegetationMask (the plane count)
import engine.vegetation;              // TerrainVegetationComponent(Manager)
import editor.core;
import editor.app;                     // IViewportToolPanelProvider, ViewportToolPanelRegistry, the widget kit
import editor.viewporttools;           // IViewportTool + ViewportToolHostContext

using namespace foundation::core;

namespace editor
{
    namespace ui = foundation::ui;
    namespace scene = foundation::scene;

    namespace
    {
        // The plane count of the scene's first resolved vegetation mask (0 = none resolved).
        [[nodiscard]] u32 ResolvePlaneCount(scene::Scene& sc)
        {
            scene::ComponentManagerBase* base = sc.FindManagerByComponentType(
                TypeOf<engine::vegetation::TerrainVegetationComponent>());
            auto* mgr = static_cast<engine::vegetation::TerrainVegetationComponentManager*>(base);
            if (mgr == nullptr)
            {
                return 0;
            }
            u32 planes = 0;
            mgr->ForEach(
                [&](engine::vegetation::TerrainVegetationComponent& c, scene::EntityHandle)
                {
                    if (planes == 0 && c.mask.Get() != nullptr && !c.mask.Get()->IsEmpty())
                    {
                        planes = c.mask.Get()->PlaneCount();
                    }
                });
            return planes;
        }

        class VegetationPaintPanelProvider final : public IViewportToolPanelProvider
        {
        public:
            [[nodiscard]] StringView ToolId() const override { return u8"vegetation.paint"; }
            [[nodiscard]] ToolPanelPlacement Placement() const override
            {
                return ToolPanelPlacement::Float;
            }
            [[nodiscard]] RefPtr<ui::View> CreatePanel(IViewportTool& tool,
                                                       const ViewportToolHostContext& ctx) override
            {
                VegetationPaintTool* t = static_cast<VegetationPaintTool*>(&tool);
                const u32 resolved = (ctx.scene != nullptr) ? ResolvePlaneCount(*ctx.scene) : 0u;
                const i32 planeCount = resolved > 0 ? static_cast<i32>(resolved) : 4;

                auto root = app::MakeToolPanelRoot();
                root->AddView(app::MakeToolPanelRow(u8"Mask plane", 12.0f).Get());
                // Slots 0..N-1 = the planes; slot N = the eraser; slot N+1 = smooth.
                auto planes = MakeRef<app::SegmentedToggle>(editor::EditorRootAllocator());
                planes->Build(
                    planeCount + 2,
                    [planeCount](i32 i) -> RefPtr<ui::View>
                    {
                        const String text = i == planeCount       ? String(u8"E")
                                            : i == planeCount + 1 ? String(u8"S")
                                                                  : Format(u8"{}", i);
                        auto label = MakeRef<ui::Label>(editor::EditorRootAllocator(), text.AsView());
                        label->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(label.Get());
                    },
                    [t, planeCount](i32 i)
                    {
                        if (i == planeCount)
                        {
                            t->SetEraser(true);
                        }
                        else if (i == planeCount + 1)
                        {
                            t->SetSmooth(true);
                        }
                        else
                        {
                            t->SetPlane(static_cast<u32>(i));
                        }
                    },
                    [t, planeCount]() -> i32
                    {
                        return t->IsEraser()   ? planeCount
                               : t->IsSmooth() ? planeCount + 1
                                               : static_cast<i32>(t->Plane());
                    },
                    [planeCount](i32 i) -> StringView
                    {
                        if (i == planeCount)
                        {
                            return u8"Eraser (nothing grows)";
                        }
                        if (i == planeCount + 1)
                        {
                            return u8"Smooth (feathers a painted edge)";
                        }
                        return u8"Paint this plane (a layer with Mask placement names it)";
                    });
                root->AddView(planes.Get());

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(editor::EditorRootAllocator());
                RefPtr<ui::toolkit::FloatEditor> radiusField = app::AddToolPanelFloat(
                    *grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                    [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                app::AddToolPanelFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 1.0,
                                       0.05, 2, [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
                app::AddToolPanelFloat(*grid, u8"Spacing", static_cast<f64>(t->Spacing()), 0.05, 1.0,
                                       0.05, 2, [t](f64 v) { t->SetSpacing(static_cast<f32>(v)); });
                app::AddToolPanelBool(*grid, u8"Airbrush", t->IsAirbrush(),
                                      [t](bool v) { t->SetAirbrush(v); });
                t->OnRadiusChanged = [radiusField](f32 r)
                { radiusField->SetValue(static_cast<f64>(r)); };
                app::AddToolPanelGrid(*root, grid);
                return root;
            }
        };
    }

    void RegisterVegetationToolPanels()
    {
        static VegetationPaintPanelProvider paintPanel;
        ViewportToolPanelRegistry::Get().Register(&paintPanel);
    }
}
