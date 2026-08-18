// Engine::Navigation - implementation unit.
//
// The component reflection (REFLECT_VALUE bodies) lives here, out of the interface units (GCC
// module hygiene). Surfaces the zone + agent components to the editor inspector (displayName/
// category, the zone Ref picker) and the agent's runtime API to script
// (NavAgent.of(entity).navigate(x,y,z) / .stop() / .finished() / .remaining() / .velocity*()).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module engine.navigation;

import foundation.core;
import foundation.resource;
import foundation.navigation.resource;
import foundation.script.facades;

using namespace foundation::core;

namespace engine::navigation
{
    REFLECT_VALUE(NavMeshZoneComponent, "rtti::engine::navigation")
    {
        builder.Attribute("displayName", String(u8"Nav Mesh Zone"))
            .Attribute("category", String(u8"Navigation"))
            .DataVersion(1)
            .Property<&NavMeshZoneComponent::extents>("extents")
            .Property<&NavMeshZoneComponent::cellSize>("cellSize")
            .Property<&NavMeshZoneComponent::cellHeight>("cellHeight")
            .Property<&NavMeshZoneComponent::agentRadius>("agentRadius")
            .Property<&NavMeshZoneComponent::agentHeight>("agentHeight")
            .Property<&NavMeshZoneComponent::agentMaxClimb>("agentMaxClimb")
            .Property<&NavMeshZoneComponent::agentMaxSlopeDegrees>("agentMaxSlopeDegrees")
            .Property<&NavMeshZoneComponent::zone>("zone");
    }

    REFLECT_VALUE(NavAgentComponent, "rtti::engine::navigation")
    {
        builder.Attribute("displayName", String(u8"Nav Agent"))
            .Attribute("category", String(u8"Navigation"))
            .DataVersion(1)
            .Property<&NavAgentComponent::radius>("radius")
            .Property<&NavAgentComponent::height>("height")
            .Property<&NavAgentComponent::maxSpeed>("maxSpeed")
            .Property<&NavAgentComponent::maxAcceleration>("maxAcceleration")
            .Property<&NavAgentComponent::moveEntity>("moveEntity")
            // Runtime API (Track A): NavAgent.of(entity) -> a re-resolving handle.
            .Method<&foundation::script::ComponentOf<NavAgentComponent>, NavAgentComponent>("of")
            .Method<&NavAgentComponent::navigate>("navigate", {"x", "y", "z"})
            .Method<&NavAgentComponent::stop>("stop")
            .Method<&NavAgentComponent::finishedNav>("finished")
            .Method<&NavAgentComponent::remaining>("remaining")
            .Method<&NavAgentComponent::velocityX>("velocityX")
            .Method<&NavAgentComponent::velocityY>("velocityY")
            .Method<&NavAgentComponent::velocityZ>("velocityZ");
    }

    void RegisterNavigationComponentReflection()
    {
        static const bool once = []()
        {
            RttiRegisterValue_NavMeshZoneComponent();
            RttiRegisterValue_NavAgentComponent();
            return true;
        }();
        (void)once;
    }
}
