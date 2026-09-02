// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::Navigation - :components partition.
//
// The authoring components:
//   * NavMeshZoneComponent - a navmesh zone: AABB half-extents (the bake region), the bake
//     params (used by the editor's Bake action; the runtime ignores them), and a Ref to the
//     cooked NavigationZoneResource the subsystem loads.
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
        foundation::resource::Ref<nav::NavigationZoneResource> zone;

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
        // Arrival radius: the agent counts as finished (and stops steering) within this
        // distance of the target - "walk NEAR the door", follow-at-distance, surround
        // behaviors. 0 = the legacy walk-onto-the-point arrival.
        f32 stopDistance = 0.0f;
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
        f32 appliedSpeed = -1.0f;        // steering profile last pushed into the crowd - the
        f32 appliedAcceleration = -1.0f; // tick re-applies on ANY change (script or inspector)
        // Introspection cache (filled from the crowd each tick; see the foundation enums):
        u8 crowdState = 0;       // NavAgentCrowdState: 0 invalid, 1 walking, 2 off-mesh
        u8 crowdTargetState = 0; // NavAgentTargetState: 0 none, 1 requesting, 2 valid,
                                 // 3 velocity, 4 failed
        f32 crowdDesiredSpeed = 0.0f; // the crowd's current speed intent
        i32 pathCorners = 0;          // corridor corners ahead (path progress hint)

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
        // Per-call speed control (Lumix parity): applies to the LIVE crowd agent next tick.
        void setSpeed(f32 speed) { maxSpeed = speed; }
        void setStopDistance(f32 distance) { stopDistance = distance; }
        [[nodiscard]] f32 speed() const { return maxSpeed; }
        // navigate + speed + arrival radius in one call (the common scripted move order).
        void navigateAt(f32 x, f32 y, f32 z, f32 moveSpeed, f32 arriveDistance)
        {
            maxSpeed = moveSpeed;
            stopDistance = arriveDistance;
            navigate(x, y, z);
        }
        // Introspection (Lumix parity): WHY an agent is/is not moving. Numeric contracts
        // (the enums above) - stable for script logic and logging.
        [[nodiscard]] i32 state() const { return static_cast<i32>(crowdState); }
        [[nodiscard]] i32 targetState() const { return static_cast<i32>(crowdTargetState); }
        [[nodiscard]] f32 desiredSpeed() const { return crowdDesiredSpeed; }
        [[nodiscard]] i32 corners() const { return pathCorners; }
        [[nodiscard]] bool finishedNav() const { return finished; }
        [[nodiscard]] f32 remaining() const { return remainingDistance; }
        [[nodiscard]] f32 velocityX() const { return desiredVelocity.x; }
        [[nodiscard]] f32 velocityY() const { return desiredVelocity.y; }
        [[nodiscard]] f32 velocityZ() const { return desiredVelocity.z; }
    };

    // Per-scene navigation settings (the NavigationSceneSystem's settings block). `debugDraw`
    // renders the loaded navmesh surface; `debugDrawPaths` adds agent target lines. Both work in
    // editor AND player (the physics-debug precedent), gated here, drawn by the runtime subsystem.
    struct NavigationSceneSettings
    {
        bool debugDraw = false;
        bool debugDrawPaths = false;
    };

    inline void SerializeNavigationSceneSettings(ISerializer& ar, NavigationSceneSettings& settings)
    {
        foundation::core::Serialize(ar, "debugDraw", settings.debugDraw);
        foundation::core::Serialize(ar, "debugDrawPaths", settings.debugDrawPaths);
    }

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
        if (ar.Version() >= 2) // v2: arrival radius (stopDistance)
        {
            foundation::core::Serialize(ar, "stopDistance", c.stopDistance);
        }
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
