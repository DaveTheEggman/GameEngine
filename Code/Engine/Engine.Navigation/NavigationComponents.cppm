// Engine::Navigation - :components partition.
//
// The authoring components (Documentation/Plans/navigation.md):
//   * NavMeshZoneComponent - a navmesh zone: AABB half-extents (the bake region), the bake
//     params (used by the editor's Bake action; the runtime ignores them), and a Ref to the
//     cooked NavigationZone the subsystem loads.
//   * NavAgentComponent - an agent that steers to targets through the crowd. Reflected runtime
//     methods (navigate/stop/finished/remaining/velocity) write intent to transient fields; the
//     NavigationSceneSystem tick consumes them and (in MoveEntity mode) writes the transform.
//
// Runtime fields (agent id, zone index, target request, status) are transient - never serialized.

module;
#include "Core/Prelude.h"

export module engine.navigation:components;

import foundation.core;
import foundation.scene;
import foundation.resource;
import foundation.navigation.resource;

using namespace foundation::core;

export namespace engine::navigation
{
    namespace nav = foundation::navigation;

    struct NavMeshZoneComponent
    {
        // Authored: the zone AABB half-extents in the entity's local space (the bake region).
        Float3 extents{20.0f, 10.0f, 20.0f};
        // Authored bake profile (the editor Bake action reads these; the runtime does not).
        f32 cellSize = 0.3f;
        f32 cellHeight = 0.2f;
        f32 agentRadius = 0.6f;
        f32 agentHeight = 2.0f;
        f32 agentMaxClimb = 0.9f;
        f32 agentMaxSlopeDegrees = 45.0f;
        // The cooked navmesh this zone loads at runtime.
        foundation::resource::Ref<nav::NavigationZone> zone;

        // Runtime (transient): index into the subsystem's live-zone list, -1 until started.
        i32 runtimeIndex = -1;
    };

    struct NavAgentComponent
    {
        // Authored:
        f32 radius = 0.6f;
        f32 height = 2.0f;
        f32 maxSpeed = 3.5f;
        f32 maxAcceleration = 8.0f;
        // MoveEntity (default): the agent writes the entity transform from crowd output.
        // ReportOnly: the entity is NOT moved; script/physics reads the desired velocity instead.
        bool moveEntity = true;

        // Runtime (transient):
        i32 agentId = -1;   // crowd agent handle within its zone, -1 = not registered
        i32 zoneIndex = -1; // subsystem live-zone index, -1 = outside every zone
        Float3 target{0, 0, 0};
        bool hasTarget = false;
        bool targetDirty = false;   // a navigate() request the tick has not applied yet
        bool stopRequested = false; // a stop() request the tick has not applied yet
        bool finished = true;
        f32 remainingDistance = 0.0f;
        Float3 desiredVelocity{0, 0, 0}; // crowd steering output (world space), for ReportOnly

        // --- reflected runtime API (script: NavAgent.of(entity).navigate(x,y,z) ...) ---
        // Steer toward a world-space destination.
        void navigate(f32 x, f32 y, f32 z)
        {
            target = Float3{x, y, z};
            hasTarget = true;
            targetDirty = true;
            stopRequested = false;
            finished = false;
        }
        // Halt where the agent is.
        void stop()
        {
            hasTarget = false;
            stopRequested = true;
            finished = true;
        }
        [[nodiscard]] bool finishedNav() const { return finished; }
        [[nodiscard]] f32 remaining() const { return remainingDistance; }
        [[nodiscard]] f32 velocityX() const { return desiredVelocity.x; }
        [[nodiscard]] f32 velocityY() const { return desiredVelocity.y; }
        [[nodiscard]] f32 velocityZ() const { return desiredVelocity.z; }
    };

    inline void Serialize(ISerializer& ar, NavMeshZoneComponent& c)
    {
        foundation::core::Serialize(ar, "extents", c.extents);
        foundation::core::Serialize(ar, "cellSize", c.cellSize);
        foundation::core::Serialize(ar, "cellHeight", c.cellHeight);
        foundation::core::Serialize(ar, "agentRadius", c.agentRadius);
        foundation::core::Serialize(ar, "agentHeight", c.agentHeight);
        foundation::core::Serialize(ar, "agentMaxClimb", c.agentMaxClimb);
        foundation::core::Serialize(ar, "agentMaxSlopeDegrees", c.agentMaxSlopeDegrees);
        foundation::core::Serialize(ar, "zone", c.zone);
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager,
                                 NavMeshZoneComponent& c)
    {
        c.zone.Bind(manager);
    }

    inline void Serialize(ISerializer& ar, NavAgentComponent& c)
    {
        foundation::core::Serialize(ar, "radius", c.radius);
        foundation::core::Serialize(ar, "height", c.height);
        foundation::core::Serialize(ar, "maxSpeed", c.maxSpeed);
        foundation::core::Serialize(ar, "maxAcceleration", c.maxAcceleration);
        foundation::core::Serialize(ar, "moveEntity", c.moveEntity);
    }

    class NavMeshZoneComponentManager final
        : public foundation::scene::SerializableComponentManager<NavMeshZoneComponent>
    {
    public:
        NavMeshZoneComponentManager()
            : SerializableComponentManager<NavMeshZoneComponent>(u8"navigation.Zone")
        {
        }
    };

    class NavAgentComponentManager final
        : public foundation::scene::SerializableComponentManager<NavAgentComponent>
    {
    public:
        NavAgentComponentManager()
            : SerializableComponentManager<NavAgentComponent>(u8"navigation.Agent")
        {
        }
    };

    // Registers the component reflection (displayName/category, the `.of` handle + agent methods,
    // the zone Ref picker). Defined in the implementation unit (REFLECT_VALUE bodies stay out of
    // interface units - GCC module hygiene).
    void RegisterNavigationComponentReflection();

    // Metadata-only script-facade registrar (DefaultApp + Engine.ScriptSurface call it): surfaces
    // NavAgentComponent to the script backends so behaviors can call NavAgent.of(entity).navigate(...)
    // and reference the class name in their prelude. Touches no device/world.
    void RegisterNavigationScriptFacade();
}
