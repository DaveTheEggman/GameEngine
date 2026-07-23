// Draconic::EditorScene - :component_gizmos partition.
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

export module draconic.editor.scene:component_gizmos;

import draconic.core;
import draconic.scene;
import draconic.render;
import draconic.render.subsystem;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace scene = draconic::scene;
    namespace render = draconic::render;

    /// Drawing context passed to gizmo renderers.
    struct GizmoContext
    {
        render::debug::DebugDraw* debug = nullptr;
        scene::Scene* scene = nullptr;
        Float3 cameraPosition{};
    };

    /// A viewport gizmo for one component type. Registered per scene-editor module; the page
    /// draws the selected entity's components through the registry (and every entity's for
    /// renderers that opt into DrawWhenUnselected).
    class IGizmoRenderer
    {
    public:
        virtual ~IGizmoRenderer() = default;
        [[nodiscard]] virtual const TypeInfo* ComponentType() const = 0;
        virtual void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) = 0;
        [[nodiscard]] virtual bool DrawWhenUnselected() const { return false; }
    };

    class GizmoRendererRegistry
    {
    public:
        void Register(UniquePtr<IGizmoRenderer> renderer)
        {
            if (renderer) { m_renderers.PushBack(Move(renderer)); }
        }

        [[nodiscard]] IGizmoRenderer* Find(const TypeInfo* componentType) const
        {
            for (const UniquePtr<IGizmoRenderer>& r : m_renderers)
            {
                if (r->ComponentType() == componentType) { return r.Get(); }
            }
            return nullptr;
        }

        /// Draw gizmos for `entity`'s components; when `selected` is false only renderers with
        /// DrawWhenUnselected participate.
        void DrawEntity(scene::EntityHandle entity, bool selected, GizmoContext& ctx) const
        {
            if (ctx.scene == nullptr || ctx.debug == nullptr || !entity.IsAssigned()) { return; }
            ctx.scene->ForEachManager([&](scene::ComponentManagerBase& mgr) {
                if (!mgr.HasComponent(entity)) { return; }
                IGizmoRenderer* renderer = Find(mgr.ComponentType());
                if (renderer == nullptr) { return; }
                if (!selected && !renderer->DrawWhenUnselected()) { return; }
                const Instance component = mgr.GetComponentInstance(entity);
                if (!component.IsEmpty()) { renderer->Draw(component, entity, ctx); }
            });
        }

        [[nodiscard]] usize Count() const noexcept { return m_renderers.Size(); }

    private:
        Array<UniquePtr<IGizmoRenderer>> m_renderers;
    };

    namespace detail
    {
        inline Float3 WorldPosition(const Float4x4& world)
        {
            return Float3{ world.m[3][0], world.m[3][1], world.m[3][2] };
        }

        inline Float3 WorldForward(const Float4x4& world)   // -Z basis row, normalized
        {
            return Normalized(Float3{ -world.m[2][0], -world.m[2][1], -world.m[2][2] });
        }

        inline void DrawCenterCross(render::debug::DebugDraw& dd, Float3 p, f32 r, Color color)
        {
            dd.DrawLine(p - Float3{ r, 0, 0 }, p + Float3{ r, 0, 0 }, color);
            dd.DrawLine(p - Float3{ 0, r, 0 }, p + Float3{ 0, r, 0 }, color);
            dd.DrawLine(p - Float3{ 0, 0, r }, p + Float3{ 0, 0, r }, color);
        }
    }

    /// Light wireframes: directional = sun cross + direction arrow; point = range sphere;
    /// spot = cone (tip circle at range with the outer half-angle + four apex rays).
    class LightGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override
        {
            return &TypeOf<render::LightComponent>();
        }

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override
        {
            const auto* light = component.TryGet<render::LightComponent>();
            if (light == nullptr) { return; }

            const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
            const Float3 position = detail::WorldPosition(world);
            const Float3 forward = detail::WorldForward(world);
            render::debug::DebugDraw& dd = *ctx.debug;
            const Color color{ Clamp(light->color.r, 0.0f, 1.0f), Clamp(light->color.g, 0.0f, 1.0f),
                               Clamp(light->color.b, 0.0f, 1.0f), 1.0f };

            switch (light->type)
            {
                case render::LightType::Directional:
                {
                    detail::DrawCenterCross(dd, position, 0.3f, color);
                    const Float3 tip = position + forward * 1.5f;
                    dd.DrawArrow(position, tip, color, 0.2f);
                    break;
                }
                case render::LightType::Point:
                {
                    dd.DrawWireSphere(position, light->range, color, 24);
                    detail::DrawCenterCross(dd, position, 0.15f, color);
                    break;
                }
                case render::LightType::Spot:
                {
                    const f32 tipDist = Max(light->range, 0.1f);
                    const Float3 tipCenter = position + forward * tipDist;
                    const f32 tipRadius = tipDist * Tan(light->outerAngle);

                    const Float3 up = (Abs(forward.y) < 0.99f) ? Float3{ 0, 1, 0 } : Float3{ 0, 0, 1 };
                    const Float3 right = Normalized(Cross(forward, up));
                    const Float3 trueUp = Cross(right, forward);

                    dd.DrawCircle(tipCenter, right, trueUp, tipRadius, color, 24);
                    dd.DrawLine(position, tipCenter + right * tipRadius, color);
                    dd.DrawLine(position, tipCenter - right * tipRadius, color);
                    dd.DrawLine(position, tipCenter + trueUp * tipRadius, color);
                    dd.DrawLine(position, tipCenter - trueUp * tipRadius, color);
                    break;
                }
            }
        }
    };

    /// Reflection probe: wire influence box from halfExtents (entity-oriented) + center cross.
    class ReflectionProbeGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override
        {
            return &TypeOf<render::ReflectionProbeComponent>();
        }

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override
        {
            const auto* probe = component.TryGet<render::ReflectionProbeComponent>();
            if (probe == nullptr) { return; }

            const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
            const Float3 position = detail::WorldPosition(world);
            render::debug::DebugDraw& dd = *ctx.debug;
            const Color color{ 0.4f, 0.8f, 1.0f, 1.0f };

            dd.DrawTransformedBox(Float3{} - probe->halfExtents, probe->halfExtents, world, color);
            detail::DrawCenterCross(dd, position, 0.2f, color);
        }
    };

    /// Decal projection volume: the oriented box the decal clips to (local [-size/2, size/2],
    /// entity-oriented) + an arrow along local +Z, the projection direction. Decals only land on
    /// surfaces INSIDE this box facing (within the angle fade) against the arrow - the gizmo is
    /// what makes "why doesn't my decal show" placement mistakes visible.
    class DecalGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override
        {
            return &TypeOf<render::DecalComponent>();
        }

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override
        {
            const auto* decal = component.TryGet<render::DecalComponent>();
            if (decal == nullptr) { return; }

            const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
            const Float3 position = detail::WorldPosition(world);
            render::debug::DebugDraw& dd = *ctx.debug;
            const Color color{ 1.0f, 0.75f, 0.2f, 1.0f };

            const Float3 he = decal->size * 0.5f;
            dd.DrawTransformedBox(Float3{} - he, he, world, color);
            // Projection direction: local +Z (the opposite of the camera-style forward).
            const Float3 projDir = Float3{} - detail::WorldForward(world);
            dd.DrawArrow(position, position + projDir * (he.z + 0.35f), color, 0.12f);
        }
    };

    /// Camera frustum wireframe from the component's projection at the entity's pose.
    class CameraGizmoRenderer final : public IGizmoRenderer
    {
    public:
        [[nodiscard]] const TypeInfo* ComponentType() const override
        {
            return &TypeOf<render::CameraComponent>();
        }

        void Draw(const Instance& component, scene::EntityHandle owner, GizmoContext& ctx) override
        {
            const auto* camera = component.TryGet<render::CameraComponent>();
            if (camera == nullptr) { return; }

            const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
            const Float3 position = detail::WorldPosition(world);
            const Float3 forward = detail::WorldForward(world);
            const Float3 up = (Abs(forward.y) < 0.99f) ? Float3{ 0, 1, 0 } : Float3{ 0, 0, 1 };

            // Short preview frustum (clamped far) so scene cameras stay readable.
            const f32 farZ = Min(camera->farZ, 8.0f);
            const Float4x4 view = Float4x4::LookAtRH(position, position + forward, up);
            const Float4x4 proj = Float4x4::PerspectiveFovRH(camera->fovYRadians, camera->aspect,
                                                             Max(camera->nearZ, 0.01f), farZ);
            ctx.debug->DrawFrustum(Inverse(view * proj), Color{ 0.9f, 0.9f, 0.9f, 1.0f });
        }
    };

    /// Register the built-in component gizmos (called from RegisterSceneEditor).
    inline void RegisterBuiltinGizmoRenderers(GizmoRendererRegistry& registry)
    {
        registry.Register(UniquePtr<IGizmoRenderer>(
            DefaultAllocator().New<LightGizmoRenderer>(), DefaultAllocator()));
        registry.Register(UniquePtr<IGizmoRenderer>(
            DefaultAllocator().New<ReflectionProbeGizmoRenderer>(), DefaultAllocator()));
        registry.Register(UniquePtr<IGizmoRenderer>(
            DefaultAllocator().New<CameraGizmoRenderer>(), DefaultAllocator()));
        registry.Register(UniquePtr<IGizmoRenderer>(
            DefaultAllocator().New<DecalGizmoRenderer>(), DefaultAllocator()));
    }
}
