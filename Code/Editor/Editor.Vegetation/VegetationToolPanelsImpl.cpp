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
import foundation.vegetation;          // VegetationPlacement (which layers are Scattered)
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
                // Two rows: the PLANE the brush works on (always lit, erase included - erase is
                // per plane) and the MODE.
                root->AddView(app::MakeToolPanelRow(u8"Mask plane", 12.0f).Get());
                auto planes = MakeRef<app::SegmentedToggle>(editor::EditorRootAllocator());
                planes->Build(
                    planeCount,
                    [](i32 i) -> RefPtr<ui::View>
                    {
                        const String text = Format(u8"{}", i);
                        auto label = MakeRef<ui::Label>(editor::EditorRootAllocator(), text.AsView());
                        label->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(label.Get());
                    },
                    [t](i32 i) { t->SetPlane(static_cast<u32>(i)); },
                    [t]() -> i32 { return static_cast<i32>(t->Plane()); },
                    [](i32) -> StringView
                    { return u8"The plane the brush works on (a layer with Mask placement names it)"; });
                root->AddView(planes.Get());
                root->AddView(app::MakeToolPanelRow(u8"Mode", 12.0f).Get());
                auto modes = MakeRef<app::SegmentedToggle>(editor::EditorRootAllocator());
                modes->Build(
                    3,
                    [](i32 i) -> RefPtr<ui::View>
                    {
                        const StringView text = i == 0 ? StringView(u8"Paint")
                                                : i == 1 ? StringView(u8"Erase")
                                                         : StringView(u8"Smooth");
                        auto label = MakeRef<ui::Label>(editor::EditorRootAllocator(), text);
                        label->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(label.Get());
                    },
                    [t](i32 i)
                    {
                        t->SetEraser(i == 1);
                        t->SetSmooth(i == 2);
                    },
                    [t]() -> i32 { return t->IsSmooth() ? 2 : t->IsEraser() ? 1 : 0; },
                    [](i32 i) -> StringView
                    {
                        return i == 0   ? StringView(u8"Paint the plane (density up)")
                               : i == 1 ? StringView(u8"Erase the plane under the brush (nothing grows)")
                                        : StringView(u8"Smooth (feathers a painted edge)");
                    });
                root->AddView(modes.Get());

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

    namespace
    {
        // The names of the scene's first vegetation component's layers, with which are Scattered.
        struct LayerNames
        {
            Array<String> names;
            Array<bool> scattered;
            Array<bool> hasMesh; // the mesh reference resolves (a stale asset does not)
        };
        [[nodiscard]] LayerNames ResolveLayerNames(scene::Scene& sc)
        {
            LayerNames out;
            scene::ComponentManagerBase* base = sc.FindManagerByComponentType(
                TypeOf<engine::vegetation::TerrainVegetationComponent>());
            auto* mgr = static_cast<engine::vegetation::TerrainVegetationComponentManager*>(base);
            if (mgr == nullptr)
            {
                return out;
            }
            bool taken = false;
            mgr->ForEach(
                [&](engine::vegetation::TerrainVegetationComponent& c, scene::EntityHandle)
                {
                    if (taken || c.layers.IsEmpty())
                    {
                        return;
                    }
                    taken = true;
                    for (usize i = 0; i < c.layers.Size(); ++i)
                    {
                        out.names.PushBack(c.layers[i].name.IsEmpty() ? Format(u8"Layer {}", i + 1)
                                                                      : c.layers[i].name);
                        out.scattered.PushBack(c.layers[i].placement ==
                                               foundation::vegetation::VegetationPlacement::Scattered);
                        out.hasMesh.PushBack(c.layers[i].mesh.Get() != nullptr);
                    }
                });
            return out;
        }

        // ---- Paint Props panel: the Scattered layer choices + E, then radius / density /
        // strength / spacing ---------------------------------------------------------------
        class VegetationScatterPanelProvider final : public IViewportToolPanelProvider
        {
        public:
            [[nodiscard]] StringView ToolId() const override { return u8"vegetation.scatter"; }
            [[nodiscard]] ToolPanelPlacement Placement() const override
            {
                return ToolPanelPlacement::Float;
            }
            [[nodiscard]] RefPtr<ui::View> CreatePanel(IViewportTool& tool,
                                                       const ViewportToolHostContext& ctx) override
            {
                VegetationScatterTool* t = static_cast<VegetationScatterTool*>(&tool);
                const LayerNames layers =
                    (ctx.scene != nullptr) ? ResolveLayerNames(*ctx.scene) : LayerNames{};
                const i32 count = static_cast<i32>(layers.names.Size());

                auto root = app::MakeToolPanelRoot();
                // Two rows: the LAYER the brush works on (always lit, erase included - erase is
                // per layer) and the MODE.
                root->AddView(app::MakeToolPanelRow(u8"Prop layer (Scattered)", 12.0f).Get());
                if (count == 0)
                {
                    root->AddView(app::MakeToolPanelRow(
                                      u8"No vegetation layers here - add a Scattered layer", 11.0f)
                                      .Get());
                }
                auto choices = MakeRef<app::SegmentedToggle>(editor::EditorRootAllocator());
                choices->Build(
                    count,
                    [names = layers.names, scattered = layers.scattered,
                     hasMesh = layers.hasMesh](i32 i) -> RefPtr<ui::View>
                    {
                        String text = names[static_cast<usize>(i)];
                        if (!scattered[static_cast<usize>(i)])
                        {
                            text = Format(u8"{} (not scattered)", text.AsView());
                        }
                        else if (!hasMesh[static_cast<usize>(i)])
                        {
                            text = Format(u8"{} (no mesh)", text.AsView()); // nothing would draw
                        }
                        auto label = MakeRef<ui::Label>(editor::EditorRootAllocator(), text.AsView());
                        label->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(label.Get());
                    },
                    [t](i32 i) { t->SetLayer(static_cast<u32>(i)); },
                    [t]() -> i32 { return static_cast<i32>(t->Layer()); },
                    [](i32) -> StringView { return u8"The layer the brush works on"; });
                root->AddView(choices.Get());
                root->AddView(app::MakeToolPanelRow(u8"Mode", 12.0f).Get());
                auto modes = MakeRef<app::SegmentedToggle>(editor::EditorRootAllocator());
                modes->Build(
                    2,
                    [](i32 i) -> RefPtr<ui::View>
                    {
                        const StringView text = i == 0 ? StringView(u8"Paint") : StringView(u8"Erase");
                        auto label = MakeRef<ui::Label>(editor::EditorRootAllocator(), text);
                        label->FontSize.SetValue(Optional<f32>{12.0f});
                        return RefPtr<ui::View>(label.Get());
                    },
                    [t](i32 i) { t->SetEraser(i == 1); },
                    [t]() -> i32 { return t->IsEraser() ? 1 : 0; },
                    [](i32 i) -> StringView
                    {
                        return i == 0 ? StringView(u8"Place props into the layer")
                                      : StringView(u8"Remove the layer's props under the brush");
                    });
                root->AddView(modes.Get());

                auto grid = MakeRef<ui::toolkit::PropertyGrid>(editor::EditorRootAllocator());
                RefPtr<ui::toolkit::FloatEditor> radiusField = app::AddToolPanelFloat(
                    *grid, u8"Radius", static_cast<f64>(t->Radius()), 0.5, 128.0, 1.0, 1,
                    [t](f64 v) { t->SetRadius(static_cast<f32>(v)); });
                app::AddToolPanelFloat(*grid, u8"Density /m2", static_cast<f64>(t->Density()), 0.0,
                                       64.0, 0.05, 2, [t](f64 v) { t->SetDensity(static_cast<f32>(v)); });
                app::AddToolPanelFloat(*grid, u8"Strength", static_cast<f64>(t->Strength()), 0.0, 1.0,
                                       0.05, 2, [t](f64 v) { t->SetStrength(static_cast<f32>(v)); });
                app::AddToolPanelFloat(*grid, u8"Spacing (x radius)", static_cast<f64>(t->Spacing()),
                                       0.0, 8.0, 0.1, 1, [t](f64 v) { t->SetSpacing(static_cast<f32>(v)); });
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
        static VegetationScatterPanelProvider scatterPanel;
        ViewportToolPanelRegistry::Get().Register(&paintPanel);
        ViewportToolPanelRegistry::Get().Register(&scatterPanel);
    }
}
