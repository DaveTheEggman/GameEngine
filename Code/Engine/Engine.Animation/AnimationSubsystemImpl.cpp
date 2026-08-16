// Animation - animation.subsystem implementation unit: the component reflection bodies.
//
// Kept OUT of the :components interface partition: REFLECT_* bodies in an interface
// unit make GCC emit an unreadable gcm cluster for consumers (see gcc-module-interface-hygiene).
// Components.cppm declares RegisterAnimationComponentReflection(); this unit defines it and the
// RttiRegisterValue_* bodies.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module engine.animation;

import foundation.core;
import foundation.script;
import foundation.script.facades; // ComponentOf<T> + RegisterExtra* (the script `.of` surface, Track A)

using namespace foundation::core;
using namespace foundation::animation;
namespace core = foundation::core;

namespace engine::animation
{

    REFLECT_VALUE(SkeletalAnimationComponent, "rtti::engine::animation")
    {
        builder.Attribute("displayName", String(u8"Skeletal Animation"))
            .Attribute("category", String(u8"Animation"))
            // Script (Track A): SkeletalAnimationComponent.of(entity) -> live speed/startTime/autoPlay.
            // play/stop/setClip are player ops -> SceneAnimation.of(scene) (world ops keyed by entity).
            .Method<&foundation::script::ComponentOf<SkeletalAnimationComponent>,
                    SkeletalAnimationComponent>("of")
            .Property<&SkeletalAnimationComponent::skeleton>("skeleton")
            .Property<&SkeletalAnimationComponent::clip>("clip")
            .Property<&SkeletalAnimationComponent::speed>("speed")
            .Property<&SkeletalAnimationComponent::startTime>("startTime")
            .Property<&SkeletalAnimationComponent::autoPlay>("autoPlay");
    }

    REFLECT_VALUE(AnimationGraphComponent, "rtti::engine::animation")
    {
        builder.Attribute("displayName", String(u8"Animation Graph"))
            .Attribute("category", String(u8"Animation"))
            // Script (Track A): AnimationGraphComponent.of(entity) -> live `active`; graph params
            // (setFloat/setBool/setTrigger) are player ops -> SceneAnimation.of(scene).
            .Method<&foundation::script::ComponentOf<AnimationGraphComponent>, AnimationGraphComponent>(
                "of")
            .Property<&AnimationGraphComponent::skeleton>("skeleton")
            .Property<&AnimationGraphComponent::graph>("graph")
            .Property<&AnimationGraphComponent::active>("active");
    }

    // The scene-bound animation handle: SceneAnimation.of(scene).play/stop/setClip (single clip) +
    // setFloat/setBool/setTrigger (graph params). `of` returns SceneAnimation by value (concrete
    // cross-backend return), like ScenePhysics. All world ops keyed by entity - they reach the
    // manager-owned runtime player.
    REFLECT_VALUE(SceneAnimation, "rtti::engine::animation")
    {
        builder.Method<&SceneAnimation::play>("play", {"entity"});
        builder.Method<&SceneAnimation::stop>("stop", {"entity"});
        builder.Method<&SceneAnimation::pause>("pause", {"entity"});
        builder.Method<&SceneAnimation::resume>("resume", {"entity"});
        builder.Method<&SceneAnimation::isPlaying>("isPlaying", {"entity"});
        builder.Method<&SceneAnimation::time>("time", {"entity"});
        builder.Method<&SceneAnimation::setTime>("setTime", {"entity", "seconds"});
        builder.Method<&SceneAnimation::setClip>("setClip", {"entity", "resourceId"});
        builder.Method<&SceneAnimation::setFloat>("setFloat", {"entity", "name", "value"});
        builder.Method<&SceneAnimation::setBool>("setBool", {"entity", "name", "value"});
        builder.Method<&SceneAnimation::setTrigger>("setTrigger", {"entity", "name"});
        builder.Method<&SceneAnimation::of>("of", {"scene"});
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    REFLECT_VALUE(InstancedSkinningComponent, "rtti::engine::animation")
    {
        // Script (Track A): InstancedSkinningComponent.of(entity) -> live poseCount/speed (the crowd
        // skinning tunables). Pure data; the manager reads them each frame.
        builder
            .Method<&foundation::script::ComponentOf<InstancedSkinningComponent>,
                    InstancedSkinningComponent>("of")
            .Property<&InstancedSkinningComponent::poseCount>("poseCount")
            .Property<&InstancedSkinningComponent::speed>("speed");
    }

    REFLECT_ENUM(PropertyLoopMode, "rtti::engine::animation")
    {
        builder.Value("Once", PropertyLoopMode::Once);
        builder.Value("Loop", PropertyLoopMode::Loop);
        builder.Value("PingPong", PropertyLoopMode::PingPong);
    }

    REFLECT_VALUE(PropertyAnimatorComponent, "rtti::engine::animation")
    {
        // Reflected property-curve animation: the clip Ref needs its InspectorView ref-picker dispatch
        // entry or no picker renders (known trap). Script (Track A): .of(entity) -> live tunables +
        // (Phase G) play/stop methods.
        builder.Attribute("displayName", String(u8"Property Animator"))
            .Attribute("category", String(u8"Animation"))
            .Method<&foundation::script::ComponentOf<PropertyAnimatorComponent>,
                    PropertyAnimatorComponent>("of")
            // Playback ops for scripts: animator.of(entity).play()/stop()/pause()/... (no facade lib).
            .Method<&PropertyAnimatorComponent::play>("play")
            .Method<&PropertyAnimatorComponent::stop>("stop")
            .Method<&PropertyAnimatorComponent::pause>("pause")
            .Method<&PropertyAnimatorComponent::resume>("resume")
            .Method<&PropertyAnimatorComponent::isPlaying>("isPlaying")
            .Method<&PropertyAnimatorComponent::currentTime>("time")
            .Method<&PropertyAnimatorComponent::setTime>("setTime", {"seconds"})
            .Property<&PropertyAnimatorComponent::clip>("clip")
            .PropAttribute("displayName", String(u8"Clip"))
            .Property<&PropertyAnimatorComponent::autoplay>("autoplay")
            .PropAttribute("displayName", String(u8"Autoplay"))
            .Property<&PropertyAnimatorComponent::speed>("speed")
            .PropAttribute("displayName", String(u8"Speed"))
            .Property<&PropertyAnimatorComponent::loopMode>("loopMode")
            .PropAttribute("displayName", String(u8"Loop Mode"));
    }

    void RegisterAnimationComponentReflection()
    {
        static const bool once = []()
        {
            RttiRegisterValue_SkeletalAnimationComponent();
            RttiRegisterValue_AnimationGraphComponent();
            RttiRegisterValue_InstancedSkinningComponent();
            RttiRegisterEnum_PropertyLoopMode();
            RttiRegisterValue_PropertyAnimatorComponent();
            return true;
        }();
        (void)once;
    }

    void RegisterAnimationScriptFacade()
    {
        RegisterAnimationComponentReflection(); // ensure component TypeData (incl `of`) is built first
        // Surface the animation components to script (SkeletalAnimationComponent.of(entity), ...):
        // register them, seed the Wren emission roots, and name them for the behavior prelude.
        struct Entry
        {
            const core::TypeInfo* type;
            core::StringView name;
        };
        const Entry components[] = {
            {&core::TypeOf<SkeletalAnimationComponent>(), u8"SkeletalAnimationComponent"},
            {&core::TypeOf<AnimationGraphComponent>(), u8"AnimationGraphComponent"},
            {&core::TypeOf<InstancedSkinningComponent>(), u8"InstancedSkinningComponent"}};
        for (const Entry& component : components)
        {
            GlobalTypeRegistry().Register(*component.type);
            foundation::script::RegisterExtraScriptRootType(component.type);
            foundation::script::RegisterExtraFacadeName(component.name);
        }

        // The scene-bound animation handle (SceneAnimation.of(scene)): reflect + register + seed + name.
        RttiRegisterValue_SceneAnimation();
        GlobalTypeRegistry().Register(core::TypeOf<SceneAnimation>());
        foundation::script::RegisterExtraScriptRootType(&core::TypeOf<SceneAnimation>());
        foundation::script::RegisterExtraFacadeName(u8"SceneAnimation");
    }
}
