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

module editor.scene;

import foundation.core;
import foundation.scene;
import foundation.render;
import foundation.geometry;
import engine.render;
import engine.navigation;
import engine.physics;          // RigidBodyComponent (edit-time collider gizmo)
import foundation.physics;      // ShapeKind / MotionKind
import foundation.heightfield;  // Heightfield (ShapeKind::Heightfield bounds)
import foundation.physics.resource; // CollisionShape (ShapeKind::Cooked outline)

using namespace foundation::core;
namespace geometry = foundation::geometry;
namespace render = foundation::render;
namespace scene = foundation::scene;

namespace editor
{
    const TypeInfo* CameraGizmoRenderer::ComponentType() const
    {
        return &TypeOf<engine::render::CameraComponent>();
    }

    void CameraGizmoRenderer::Draw(const Instance& component, scene::EntityHandle owner,
                                   GizmoContext& ctx)
    {
        const auto* camera = component.TryGet<engine::render::CameraComponent>();
        if (camera == nullptr)
        {
            return;
        }

        const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
        const Float3 position = detail::WorldPosition(world);
        const Float3 forward = detail::WorldForward(world);
        const Float3 up = (Abs(forward.y) < 0.99f) ? Float3{0, 1, 0} : Float3{0, 0, 1};

        // Short preview frustum (clamped far) so scene cameras stay readable.
        const f32 farZ = Min(camera->farZ, 8.0f);
        const Float4x4 view = Float4x4::LookAtRH(position, position + forward, up);
        const Float4x4 proj = Float4x4::PerspectiveFovRH(camera->fovYRadians, camera->aspect,
                                                         Max(camera->nearZ, 0.01f), farZ);
        ctx.debug->DrawFrustum(Inverse(view * proj), Color{0.9f, 0.9f, 0.9f, 1.0f});
    }
    const TypeInfo* DecalGizmoRenderer::ComponentType() const
    {
        return &TypeOf<engine::render::DecalComponent>();
    }

    void DecalGizmoRenderer::Draw(const Instance& component, scene::EntityHandle owner,
                                  GizmoContext& ctx)
    {
        const auto* decal = component.TryGet<engine::render::DecalComponent>();
        if (decal == nullptr)
        {
            return;
        }

        const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
        const Float3 position = detail::WorldPosition(world);
        render::debug::DebugDraw& dd = *ctx.debug;
        const Color color{1.0f, 0.75f, 0.2f, 1.0f};

        const Float3 he = decal->size * 0.5f;
        dd.DrawTransformedBox(Float3{} - he, he, world, color);
        // Projection direction: local +Z (the opposite of the camera-style forward).
        const Float3 projDir = Float3{} - detail::WorldForward(world);
        dd.DrawArrow(position, position + projDir * (he.z + 0.35f), color, 0.12f);
    }

    const TypeInfo* NavMeshZoneGizmoRenderer::ComponentType() const
    {
        return &TypeOf<engine::navigation::NavMeshZoneComponent>();
    }
    void NavMeshZoneGizmoRenderer::Draw(const Instance& component, scene::EntityHandle owner,
                                        GizmoContext& ctx)
    {
        const auto* zone = component.TryGet<engine::navigation::NavMeshZoneComponent>();
        if (zone == nullptr)
        {
            return;
        }
        const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
        // The bake region: the entity-oriented box of the component's half-extents.
        ctx.debug->DrawTransformedBox(Float3{} - zone->extents, zone->extents, world,
                                      Color{0.20f, 0.85f, 1.0f, 1.0f});
    }
    void GizmoRendererRegistry::Register(UniquePtr<IGizmoRenderer> renderer)
    {
        if (renderer)
        {
            m_renderers.PushBack(Move(renderer));
        }
    }

    IGizmoRenderer* GizmoRendererRegistry::Find(const TypeInfo* componentType) const
    {
        for (const UniquePtr<IGizmoRenderer>& r : m_renderers)
        {
            if (r->ComponentType() == componentType)
            {
                return r.Get();
            }
        }
        return nullptr;
    }

    void GizmoRendererRegistry::DrawEntity(scene::EntityHandle entity, bool selected,
                                           GizmoContext& ctx) const
    {
        if (ctx.scene == nullptr || ctx.debug == nullptr || !entity.IsAssigned())
        {
            return;
        }
        ctx.scene->ForEachManager(
            [&](scene::ComponentManagerBase& mgr)
            {
                if (!mgr.HasComponent(entity))
                {
                    return;
                }
                IGizmoRenderer* renderer = Find(mgr.ComponentType());
                if (renderer == nullptr)
                {
                    return;
                }
                if (!selected && !renderer->DrawWhenUnselected())
                {
                    return;
                }
                const Instance component = mgr.GetComponentInstance(entity);
                if (!component.IsEmpty())
                {
                    renderer->Draw(component, entity, ctx);
                }
            });
    }
    const TypeInfo* LightGizmoRenderer::ComponentType() const
    {
        return &TypeOf<engine::render::LightComponent>();
    }

    void LightGizmoRenderer::Draw(const Instance& component, scene::EntityHandle owner,
                                  GizmoContext& ctx)
    {
        const auto* light = component.TryGet<engine::render::LightComponent>();
        if (light == nullptr)
        {
            return;
        }

        const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
        const Float3 position = detail::WorldPosition(world);
        const Float3 forward = detail::WorldForward(world);
        render::debug::DebugDraw& dd = *ctx.debug;
        const Color color{Clamp(light->color.r, 0.0f, 1.0f), Clamp(light->color.g, 0.0f, 1.0f),
                          Clamp(light->color.b, 0.0f, 1.0f), 1.0f};

        switch (light->type)
        {
        case engine::render::LightType::Directional:
        {
            detail::DrawCenterCross(dd, position, 0.3f, color);
            const Float3 tip = position + forward * 1.5f;
            dd.DrawArrow(position, tip, color, 0.2f);
            break;
        }
        case engine::render::LightType::Point:
        {
            dd.DrawWireSphere(position, light->range, color, 24);
            detail::DrawCenterCross(dd, position, 0.15f, color);
            break;
        }
        case engine::render::LightType::Spot:
        {
            const f32 tipDist = Max(light->range, 0.1f);
            const Float3 tipCenter = position + forward * tipDist;
            const f32 tipRadius = tipDist * Tan(light->outerAngle);

            const Float3 up = (Abs(forward.y) < 0.99f) ? Float3{0, 1, 0} : Float3{0, 0, 1};
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
    const TypeInfo* ReflectionProbeGizmoRenderer::ComponentType() const
    {
        return &TypeOf<engine::render::ReflectionProbeComponent>();
    }

    void ReflectionProbeGizmoRenderer::Draw(const Instance& component, scene::EntityHandle owner,
                                            GizmoContext& ctx)
    {
        const auto* probe = component.TryGet<engine::render::ReflectionProbeComponent>();
        if (probe == nullptr)
        {
            return;
        }

        const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
        const Float3 position = detail::WorldPosition(world);
        render::debug::DebugDraw& dd = *ctx.debug;
        const Color color{0.4f, 0.8f, 1.0f, 1.0f};

        dd.DrawTransformedBox(Float3{} - probe->halfExtents, probe->halfExtents, world, color);
        detail::DrawCenterCross(dd, position, 0.2f, color);
    }

    const TypeInfo* LodOverlayGizmoRenderer::ComponentType() const
    {
        return &TypeOf<engine::render::MeshComponent>();
    }

    void LodOverlayGizmoRenderer::Draw(const Instance& component, scene::EntityHandle owner,
                                       GizmoContext& ctx)
    {
        if (!ctx.lodOverlay || ctx.viewCamera == nullptr)
        {
            return;
        }
        const auto* mc = component.TryGet<engine::render::MeshComponent>();
        if (mc == nullptr)
        {
            return;
        }
        const geometry::StaticMesh* mesh = mc->mesh.Get();
        if (mesh == nullptr || mesh->lodCount <= 1)
        {
            return;
        }
        // World bounds the way extraction sees them: local AABB center through the entity
        // world; radius = half the transformed box diagonal (conservative, close enough
        // for the same pick the renderer makes).
        const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
        const Float3 center = TransformPoint(mesh->bounds.Center(), world);
        Float3 minCorner = center;
        Float3 maxCorner = center;
        const Float3 lo = mesh->bounds.min;
        const Float3 hi = mesh->bounds.max;
        for (u32 corner = 0; corner < 8; ++corner)
        {
            const Float3 local{(corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                               (corner & 4) ? hi.z : lo.z};
            const Float3 p = TransformPoint(local, world);
            minCorner = Float3{Min(minCorner.x, p.x), Min(minCorner.y, p.y), Min(minCorner.z, p.z)};
            maxCorner = Float3{Max(maxCorner.x, p.x), Max(maxCorner.y, p.y), Max(maxCorner.z, p.z)};
        }
        const f32 radius = 0.5f * Length(maxCorner - minCorner);
        const f32 coverage =
            render::LodCoverageFor(*ctx.viewCamera, center, radius, mc->lodBias);
        const u32 maxLod = mesh->lodCount - 1;
        const u32 level =
            (mc->forceLod >= 0)
                ? ((static_cast<u32>(mc->forceLod) < maxLod) ? static_cast<u32>(mc->forceLod)
                                                             : maxLod)
                : render::PickLodLevel(*mesh, coverage);
        static const Color kLevelColors[] = {
            Color{0.3f, 0.9f, 0.3f, 1.0f}, // 0 = green (finest)
            Color{0.95f, 0.9f, 0.2f, 1.0f}, // 1 = yellow
            Color{1.0f, 0.6f, 0.15f, 1.0f}, // 2 = orange
            Color{1.0f, 0.25f, 0.2f, 1.0f}, // 3+ = red
        };
        const Color color = kLevelColors[(level < 3u) ? level : 3u];
        ctx.debug->DrawTransformedBox(mesh->bounds.min, mesh->bounds.max, world, color);
    }

    const TypeInfo* PhysicsColliderGizmoRenderer::ComponentType() const
    {
        return &TypeOf<engine::physics::RigidBodyComponent>();
    }

    // Draw the collider's shape wireframe from the component data + the entity's world transform -
    // no physics world, no Jolt body (edit time). Colours are STATIC (dynamic=green, static/
    // kinematic=blue, trigger=yellow) since there is no live body to query awake/sleeping.
    void PhysicsColliderGizmoRenderer::Draw(const Instance& component, scene::EntityHandle owner,
                                            GizmoContext& ctx)
    {
        if (!ctx.showColliders) // the editor "Show Colliders" toggle (runtime debug draw is separate)
        {
            return;
        }
        const auto* body = component.TryGet<engine::physics::RigidBodyComponent>();
        if (body == nullptr)
        {
            return;
        }
        using foundation::physics::MotionKind;
        using foundation::physics::ShapeKind;

        const Float4x4 world = ctx.scene->GetWorldMatrix(owner);
        Float3 position, scale;
        Quaternion rotation;
        if (!Decompose(world, position, rotation, scale))
        {
            return;
        }
        const Color color = body->isTrigger ? Color{1.0f, 0.8f, 0.2f, 1.0f}
                            : body->motion == MotionKind::Dynamic ? Color{0.3f, 1.0f, 0.4f, 1.0f}
                                                                  : Color{0.4f, 0.6f, 1.0f, 1.0f};
        // The rigid (unscaled) frame - primitive sizes are absolute half-extents/radii, like runtime.
        const Float4x4 rigid = Transform{position, rotation, Float3{1, 1, 1}}.ToMatrix();
        render::debug::DebugDraw& dd = *ctx.debug;
        switch (body->shape)
        {
        case ShapeKind::Box:
            dd.DrawTransformedBox(Float3{} - body->halfExtents, body->halfExtents, rigid, color);
            break;
        case ShapeKind::Sphere:
            dd.DrawWireSphere(position, body->radius, color);
            break;
        case ShapeKind::Capsule:
            dd.DrawWireSphere(position, body->radius, color);
            dd.DrawTransformedBox(
                Float3{-body->radius, -(body->halfHeight + body->radius), -body->radius},
                Float3{body->radius, body->halfHeight + body->radius, body->radius}, rigid, color);
            break;
        case ShapeKind::Plane:
        {
            const f32 extent = body->planeHalfExtent < 25.0f ? body->planeHalfExtent : 25.0f;
            const i32 kCells = 10;
            for (i32 g = -kCells; g <= kCells; ++g)
            {
                const f32 off = extent * static_cast<f32>(g) / kCells;
                dd.DrawLine(TransformPoint(Float3{off, 0, -extent}, rigid),
                            TransformPoint(Float3{off, 0, extent}, rigid), color);
                dd.DrawLine(TransformPoint(Float3{-extent, 0, off}, rigid),
                            TransformPoint(Float3{extent, 0, off}, rigid), color);
            }
            break;
        }
        case ShapeKind::Heightfield:
            if (const auto* hf = body->heightfield.Get())
            {
                const Float2 ws = hf->WorldSize();
                dd.DrawTransformedBox(Float3{-ws.x * 0.5f, hf->MinY(), -ws.y * 0.5f},
                                      Float3{ws.x * 0.5f, hf->MaxY(), ws.y * 0.5f}, rigid, color);
            }
            break;
        case ShapeKind::Cooked:
            if (const auto* cooked = body->collisionShape.Get())
            {
                // Outline is authored unit-scale; re-apply the entity's scale.
                const Float4x4 shapeMatrix = Transform{position, rotation, scale}.ToMatrix();
                const Array<Float3>& outline = cooked->outline;
                for (usize t = 0; t + 2 < outline.Size(); t += 3)
                {
                    const Float3 a = TransformPoint(outline[t + 0], shapeMatrix);
                    const Float3 b = TransformPoint(outline[t + 1], shapeMatrix);
                    const Float3 d = TransformPoint(outline[t + 2], shapeMatrix);
                    dd.DrawLine(a, b, color);
                    dd.DrawLine(b, d, color);
                    dd.DrawLine(d, a, color);
                }
            }
            break;
        }
    }

    const TypeInfo* CharacterColliderGizmoRenderer::ComponentType() const
    {
        return &TypeOf<engine::physics::CharacterComponent>();
    }

    // The character controller's capsule from radius/halfHeight at the entity position. A fixed
    // colour (edit time has no live controller to query the on-ground state).
    void CharacterColliderGizmoRenderer::Draw(const Instance& component, scene::EntityHandle owner,
                                              GizmoContext& ctx)
    {
        if (!ctx.showColliders)
        {
            return;
        }
        const auto* ch = component.TryGet<engine::physics::CharacterComponent>();
        if (ch == nullptr)
        {
            return;
        }
        const Float3 position = detail::WorldPosition(ctx.scene->GetWorldMatrix(owner));
        const Color color{0.2f, 0.9f, 0.9f, 1.0f}; // cyan
        render::debug::DebugDraw& dd = *ctx.debug;
        dd.DrawWireSphere(Float3{position.x, position.y + ch->halfHeight, position.z}, ch->radius,
                          color);
        dd.DrawWireSphere(Float3{position.x, position.y - ch->halfHeight, position.z}, ch->radius,
                          color);
        dd.DrawWireBoxCenter(position, Float3{ch->radius, ch->halfHeight, ch->radius}, color);
    }
}
