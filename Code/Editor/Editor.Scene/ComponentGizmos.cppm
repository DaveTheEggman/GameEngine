// Editor::Scene - :component_gizmos partition.
//
// IGizmoRenderer + registry: per-component-type viewport gizmos drawn through debug-draw
// (design doc §8). Ported from Sedulous.Editor (IGizmoRenderer/GizmoContext + the light and
// reflection-probe renderers) with fixes for our components:
//   - the probe gizmo draws a wire BOX from halfExtents (our probes are boxes; Sedulous drew an
//     influence sphere);
//   - DrawWhenUnselected defaults to false (Sedulous drew every light's range sphere always -
//     noisy; entity markers already anchor unselected entities).
// The interface is type-erased on reflection Instances (no Component base class here), so a
// renderer looks its data up from the manager's GetComponentInstance.

module;
#include "Core/Prelude.h"

export module editor.scene:component_gizmos;

import foundation.core;
import foundation.scene;
import foundation.render;
import engine.render;
import engine.navigation;

using namespace foundation::core;

export namespace editor
{
    namespace scene = foundation::scene;
    namespace render = foundation::render;

    /// Drawing context passed to gizmo renderers.
    struct GizmoContext
    {
        render::debug::DebugDraw* debug = nullptr;
        scene::Scene* scene = nullptr;
        Float3 cameraPosition{};
        // The viewport's full camera (view + projection) - the LOD overlay computes the
        // per-view pick with it. Null when the caller has no camera to offer.
        const render::ViewCamera* viewCamera = nullptr;
        // The page's LOD-overlay toolbar toggle: the overlay renderer draws nothing
        // without it (DrawWhenUnselected renderers run for every entity every frame).
        bool lodOverlay = false;
    };

    /// A viewport gizmo for one component type. Registered per scene-editor module; the page
    /// draws the selected entity's components through the registry (and every entity's for
    /// renderers that opt into DrawWhenUnselected).
    class IGizmoRenderer
    {
    public:
        virtual ~IGizmoRenderer() = default;
        [[nodiscard]] virtual const TypeInfo* ComponentType() const = 0;
        virtual void Draw(const Instance& component, scene::EntityHandle owner,
                          GizmoContext& ctx) = 0;
        [[nodiscard]] virtual bool DrawWhenUnselected() const { return false; }
    };

    class GizmoRendererRegistry
    {
    public:
        void Register(UniquePtr<IGizmoRenderer> renderer);

        [[nodiscard]] IGizmoRenderer* Find(const TypeInfo* componentType) const;

        /// Draw gizmos for `entity`'s components; when `selected` is false only renderers with
        /// DrawWhenUnselected participate.
        void DrawEntity(scene::EntityHandle entity, bool selected, GizmoContext& ctx) const;

        [[nodiscard]] usize Count() const noexcept { return m_renderers.Size(); }

    private:
        Array<UniquePtr<IGizmoRenderer>> m_renderers;
    };

    namespace detail
    {
        inline Float3 WorldPosition(const Float4x4& world)
        {
            return Float3{world.m[3][0], world.m[3][1], world.m[3][2]};
        }

        inline Float3 WorldForward(const Float4x4& world) // -Z basis row, normalized
        {
            return Normalized(Float3{-world.m[2][0], -world.m[2][1], -world.m[2][2]});
        }

        inline void DrawCenterCross(render::debug::DebugDraw& dd, Float3 p, f32 r, Color color)
        {
            dd.DrawLine(p - Float3{r, 0, 0}, p + Float3{r, 0, 0}, color);
            dd.DrawLine(p - Float3{0, r, 0}, p + Float3{0, r, 0}, color);
            dd.DrawLine(p - Float3{0, 0, r}, p + Float3{0, 0, r}, color);
        }
    }

    /// Light wireframes: directional = sun cross + direction arrow; point = range sphere;
    /// spot = cone (tip circle at range with the outer half-angle + four apex rays).
    class LightGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override;

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override;
    };

    /// Reflection probe: wire influence box from halfExtents (entity-oriented) + center cross.
    class ReflectionProbeGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override;

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override;
    };

    /// Decal projection volume: the oriented box the decal clips to (local [-size/2, size/2],
    /// entity-oriented) + an arrow along local +Z, the projection direction. Decals only land on
    /// surfaces INSIDE this box facing (within the angle fade) against the arrow - the gizmo is
    /// what makes "why doesn't my decal show" placement mistakes visible.
    class DecalGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override;

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override;
    };

    /// Camera frustum wireframe from the component's projection at the entity's pose.
    class CameraGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override;

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override;
    };

    /// The navigation zone's AABB extents box (local, entity-oriented) - the region the bake
    /// samples geometry from. Read-only (extents are inspector-edited); drawn when the zone is
    /// selected (DrawWhenUnselected defaults false).
    class NavMeshZoneGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override;

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override;
    };

    /// mesh-lod.md P3 debug overlay: with the toolbar toggle on, every mesh carrying a LOD
    /// chain draws its bounds tinted by the level THIS viewport's camera selects (green 0,
    /// yellow 1, orange 2, red 3+). Recomputes the RAW pick through the exported pure
    /// selection functions (no hysteresis - visualization, not the renderer's state).
    class LodOverlayGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override;
        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override;
        [[nodiscard]] bool DrawWhenUnselected() const override { return true; }
    };

    /// Register the built-in component gizmos (called from RegisterSceneEditor).
    inline void RegisterBuiltinGizmoRenderers(GizmoRendererRegistry& registry)
    {
        registry.Register(UniquePtr<IGizmoRenderer>(DefaultAllocator().New<LightGizmoRenderer>(),
                                                    DefaultAllocator()));
        registry.Register(UniquePtr<IGizmoRenderer>(
            DefaultAllocator().New<ReflectionProbeGizmoRenderer>(), DefaultAllocator()));
        registry.Register(UniquePtr<IGizmoRenderer>(DefaultAllocator().New<CameraGizmoRenderer>(),
                                                    DefaultAllocator()));
        registry.Register(UniquePtr<IGizmoRenderer>(DefaultAllocator().New<DecalGizmoRenderer>(),
                                                    DefaultAllocator()));
        registry.Register(UniquePtr<IGizmoRenderer>(
            DefaultAllocator().New<NavMeshZoneGizmoRenderer>(), DefaultAllocator()));
        registry.Register(UniquePtr<IGizmoRenderer>(
            DefaultAllocator().New<LodOverlayGizmoRenderer>(), DefaultAllocator()));
    }
}
