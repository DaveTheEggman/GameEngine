// Draconic Animation - animation.subsystem implementation unit: the component reflection bodies.
//
// Kept OUT of the :components interface partition: DRACONIC_REFLECT_* bodies in an interface
// unit make GCC emit an unreadable gcm cluster for consumers (see gcc-module-interface-hygiene).
// Components.cppm declares RegisterAnimationComponentReflection(); this unit defines it and the
// DraconicRegisterValue_* bodies.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

module draconic.engine.animation;

import draconic.core;

using namespace draconic::core;

namespace draconic::animation
{

    DRACONIC_REFLECT_VALUE(SkeletalAnimationComponent, "draconic::animation")
    {
        builder.Attribute("displayName", String(u8"Skeletal Animation"))
            .Attribute("category", String(u8"Animation")).Property<&SkeletalAnimationComponent::skeleton>("skeleton")
            .Property<&SkeletalAnimationComponent::clip>("clip")
            .Property<&SkeletalAnimationComponent::speed>("speed")
            .Property<&SkeletalAnimationComponent::startTime>("startTime")
            .Property<&SkeletalAnimationComponent::autoPlay>("autoPlay");
    }

    DRACONIC_REFLECT_VALUE(AnimationGraphComponent, "draconic::animation")
    {
        builder.Attribute("displayName", String(u8"Animation Graph"))
            .Attribute("category", String(u8"Animation")).Property<&AnimationGraphComponent::skeleton>("skeleton")
            .Property<&AnimationGraphComponent::graph>("graph")
            .Property<&AnimationGraphComponent::active>("active");
    }

    DRACONIC_REFLECT_VALUE(InstancedSkinning, "draconic::animation")
    {
        builder.Property<&InstancedSkinning::poseCount>("poseCount")
            .Property<&InstancedSkinning::speed>("speed");
    }

    void RegisterAnimationComponentReflection()
    {
        static const bool once = []()
        {
            DraconicRegisterValue_SkeletalAnimationComponent();
            DraconicRegisterValue_AnimationGraphComponent();
            DraconicRegisterValue_InstancedSkinning();
            return true;
        }();
        (void)once;
    }
}
