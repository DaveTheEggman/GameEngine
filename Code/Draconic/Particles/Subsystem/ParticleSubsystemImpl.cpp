// Draconic Particles - particles.subsystem implementation unit: the component reflection body.
//
// Kept OUT of the :components interface partition: DRACONIC_REFLECT_* bodies in an interface
// unit make GCC emit an unreadable gcm cluster for consumers (see gcc-module-interface-hygiene).
// ParticleComponents.cppm declares RegisterParticleComponentReflection(); this unit defines it.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

module draconic.particles.subsystem;

import draconic.core;

using namespace draconic::core;

namespace draconic::particles
{
    DRACONIC_REFLECT_VALUE(ParticleEffectComponent, "draconic::particles")
    {
        builder.Property<&ParticleEffectComponent::effectAsset>("effect")
            .Property<&ParticleEffectComponent::mesh>("mesh")
            .Property<&ParticleEffectComponent::material>("material")
            .Property<&ParticleEffectComponent::meshScale>("meshScale")
            .Property<&ParticleEffectComponent::lightIntensity>("lightIntensity")
            .Property<&ParticleEffectComponent::lightRange>("lightRange")
            .Property<&ParticleEffectComponent::visible>("visible");
    }

    void RegisterParticleComponentReflection()
    {
        static const bool once = []()
        {
            DraconicRegisterValue_ParticleEffectComponent();
            return true;
        }();
        (void)once;
    }
}
