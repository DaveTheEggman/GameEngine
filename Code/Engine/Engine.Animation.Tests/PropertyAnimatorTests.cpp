// engine.animation property animator: scene integration - a clip drives a reflected property on the
// owning entity's component over a simulated tick sequence; loop wrapping; failed-track disable.

#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.scene;
import foundation.propertyanimation;
import foundation.propertyanimation.resource;
import engine.animation;

using namespace foundation::core;
namespace scene = foundation::scene;
using namespace foundation::propertyanimation;
using namespace engine::animation;

// Generated below by REFLECT_VALUE(AnimTarget); forward-declared so EnsureReflected can invoke it.
void RttiRegisterValue_AnimTarget();

namespace
{
    // A target component the clip animates: a Float3 position + a scalar. Reflected so the binding
    // resolver can walk "position" / "value"; its manager is added to the scene under this type name.
    struct AnimTarget
    {
        Float3 position{0.0f, 0.0f, 0.0f};
        f32 value = 0.0f;
    };

    inline void Serialize(ISerializer& ar, AnimTarget& c)
    {
        foundation::core::Serialize(ar, "position", c.position);
        foundation::core::Serialize(ar, "value", c.value);
    }

    class AnimTargetManager final : public scene::SerializableComponentManager<AnimTarget>
    {
    public:
        AnimTargetManager() : scene::SerializableComponentManager<AnimTarget>(u8"anim_target") {}
    };

    // Build a runtime clip driving AnimTarget.position over 1s (0 -> (10,0,0)), loop.
    RefPtr<PropertyAnimationClipResource> MakePositionClip()
    {
        RefPtr<PropertyAnimationClipResource> res =
            MakeRef<PropertyAnimationClipResource>(DefaultAllocator());
        PropertyTrack t;
        t.componentType = String(u8"AnimTarget");
        t.propertyPath = String(u8"position");
        t.kind = TrackValueKind::Float3;
        CurveKey a;
        a.time = 0.0f;
        a.value = 0.0f;
        CurveKey b;
        b.time = 1.0f;
        b.value = 10.0f;
        t.channels[0].AddKey(a);
        t.channels[0].AddKey(b);
        res->clip.tracks.PushBack(Move(t));
        res->clip.loop = true;
        res->clip.duration = res->clip.ComputeDuration();
        return res;
    }

    void EnsureReflected()
    {
        static bool done = false;
        if (done)
        {
            return;
        }
        done = true;
        RegisterCoreTypes();
        RegisterAnimationComponentReflection(); // PropertyAnimatorComponent + others
        RttiRegisterValue_AnimTarget(); // apply AnimTarget's REFLECT_VALUE body (sets the type name)
        GlobalTypeRegistry().Register(TypeOf<AnimTarget>());
    }
}

REFLECT_VALUE(AnimTarget, "rtti::engine::animation::test")
{
    builder.Property<&AnimTarget::position>("position").Property<&AnimTarget::value>("value");
}

TEST_CASE("property animator: a Float3 clip drives the owning component's position over ticks")
{
    EnsureReflected();

    scene::Scene sceneObj;
    sceneObj.AddSystem<AnimTargetManager>();
    sceneObj.AddSystem<PropertyAnimatorComponentManager>();

    auto* targets = sceneObj.GetSystem<AnimTargetManager>();
    auto* animators = sceneObj.GetSystem<PropertyAnimatorComponentManager>();
    REQUIRE(targets != nullptr);
    REQUIRE(animators != nullptr);

    const scene::EntityHandle e = sceneObj.CreateEntity(u8"Mover");
    AnimTarget& target = targets->Add(e);

    RefPtr<PropertyAnimationClipResource> clip = MakePositionClip();
    PropertyAnimatorComponent& anim = animators->Add(e);
    anim.clip = clip.Get(); // direct runtime object (no resource manager in this test)
    anim.autoplay = true;

    // First tick binds + autostarts at t=0 (position ~ 0).
    sceneObj.Update(0.0f);
    CHECK(target.position.x == doctest::Approx(0.0f));

    // Advance to ~0.5s -> midpoint (5,0,0).
    sceneObj.Update(0.5f);
    CHECK(target.position.x == doctest::Approx(5.0f));

    // Advance to ~0.9s -> near the end (9,0,0).
    sceneObj.Update(0.4f);
    CHECK(target.position.x == doctest::Approx(9.0f));

    // Past the end (t=1.1) it LOOPS back to ~0.1 (x ~ 1).
    sceneObj.Update(0.2f);
    CHECK(target.position.x == doctest::Approx(1.0f).epsilon(0.05));
}

TEST_CASE("property animator: Once mode stops at the end; Loop keeps going")
{
    EnsureReflected();
    scene::Scene sceneObj;
    sceneObj.AddSystem<AnimTargetManager>();
    sceneObj.AddSystem<PropertyAnimatorComponentManager>();
    auto* targets = sceneObj.GetSystem<AnimTargetManager>();
    auto* animators = sceneObj.GetSystem<PropertyAnimatorComponentManager>();

    const scene::EntityHandle e = sceneObj.CreateEntity(u8"Once");
    targets->Add(e);
    RefPtr<PropertyAnimationClipResource> clip = MakePositionClip();
    clip->clip.loop = false;
    PropertyAnimatorComponent& anim = animators->Add(e);
    anim.clip = clip.Get();
    anim.loopMode = PropertyLoopMode::Once;

    sceneObj.Update(0.0f);
    sceneObj.Update(2.0f); // way past the end
    CHECK(targets->Get(e)->position.x == doctest::Approx(10.0f));
    CHECK_FALSE(anim.playing); // Once stopped
    // A further tick holds the end value (stopped).
    sceneObj.Update(1.0f);
    CHECK(targets->Get(e)->position.x == doctest::Approx(10.0f));
}

TEST_CASE("property animator: a track to a missing component/property is disabled, not fatal")
{
    EnsureReflected();
    scene::Scene sceneObj;
    sceneObj.AddSystem<AnimTargetManager>();
    sceneObj.AddSystem<PropertyAnimatorComponentManager>();
    auto* targets = sceneObj.GetSystem<AnimTargetManager>();
    auto* animators = sceneObj.GetSystem<PropertyAnimatorComponentManager>();

    const scene::EntityHandle e = sceneObj.CreateEntity(u8"Mixed");
    AnimTarget& target = targets->Add(e);

    // One good track (value) + one bad (unknown property) + one bad (unknown component).
    RefPtr<PropertyAnimationClipResource> clip =
        MakeRef<PropertyAnimationClipResource>(DefaultAllocator());
    {
        PropertyTrack good;
        good.componentType = String(u8"AnimTarget");
        good.propertyPath = String(u8"value");
        good.kind = TrackValueKind::Float;
        CurveKey a; a.time = 0.0f; a.value = 0.0f;
        CurveKey b; b.time = 1.0f; b.value = 8.0f;
        good.channels[0].AddKey(a);
        good.channels[0].AddKey(b);
        clip->clip.tracks.PushBack(Move(good));

        PropertyTrack badProp;
        badProp.componentType = String(u8"AnimTarget");
        badProp.propertyPath = String(u8"nonesuch");
        badProp.kind = TrackValueKind::Float;
        clip->clip.tracks.PushBack(Move(badProp));

        PropertyTrack badComp;
        badComp.componentType = String(u8"NoSuchComponent");
        badComp.propertyPath = String(u8"x");
        badComp.kind = TrackValueKind::Float;
        clip->clip.tracks.PushBack(Move(badComp));
    }
    clip->clip.duration = clip->clip.ComputeDuration();

    PropertyAnimatorComponent& anim = animators->Add(e);
    anim.clip = clip.Get();

    sceneObj.Update(0.0f);
    sceneObj.Update(0.5f); // must not crash despite the two bad tracks
    CHECK(target.value == doctest::Approx(4.0f)); // the good track still animates
}

TEST_CASE("property animator: reflected play/pause/stop/setTime drive the clock")
{
    EnsureReflected();
    scene::Scene sceneObj;
    sceneObj.AddSystem<AnimTargetManager>();
    sceneObj.AddSystem<PropertyAnimatorComponentManager>();
    auto* targets = sceneObj.GetSystem<AnimTargetManager>();
    auto* animators = sceneObj.GetSystem<PropertyAnimatorComponentManager>();

    const scene::EntityHandle e = sceneObj.CreateEntity(u8"Scripted");
    AnimTarget& target = targets->Add(e);
    RefPtr<PropertyAnimationClipResource> clip = MakePositionClip();
    PropertyAnimatorComponent& anim = animators->Add(e);
    anim.clip = clip.Get();
    anim.autoplay = false; // driven by script-style method calls, not autoplay

    // No autoplay: the first tick binds but stays stopped.
    sceneObj.Update(0.0f);
    CHECK_FALSE(anim.isPlaying());
    CHECK(target.position.x == doctest::Approx(0.0f));

    // play() -> starts from 0; a half-second tick reaches the midpoint.
    anim.play();
    CHECK(anim.isPlaying());
    sceneObj.Update(0.5f);
    CHECK(target.position.x == doctest::Approx(5.0f));

    // pause() freezes the clock: the value holds across ticks.
    anim.pause();
    CHECK_FALSE(anim.isPlaying());
    sceneObj.Update(0.5f);
    CHECK(target.position.x == doctest::Approx(5.0f));

    // setTime + resume jump the clock.
    anim.setTime(0.8f);
    anim.resume();
    sceneObj.Update(0.0f);
    CHECK(target.position.x == doctest::Approx(8.0f));

    // stop() resets to 0.
    anim.stop();
    CHECK_FALSE(anim.isPlaying());
    CHECK(anim.currentTime() == doctest::Approx(0.0f));
}
