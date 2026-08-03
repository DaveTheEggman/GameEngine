// Draconic::ScriptFacades - implementation unit: the DRACONIC_REFLECT_* bodies (they
// never sit in a module interface unit - the GCC gcm-cluster rule).

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

module draconic.script.facades;

import draconic.core;
import draconic.scene;
import draconic.script;

using namespace draconic::core;

namespace draconic::script
{
    DRACONIC_REFLECT_VALUE(Entity, "draconic::script")
    {
        builder.Method<&Entity::isValid>("isValid");
        builder.Method<&Entity::name>("name");
        builder.Method<&Entity::setName>("setName");
        builder.Method<&Entity::position>("position");
        builder.Method<&Entity::setPosition>("setPosition");
        builder.Method<&Entity::worldPosition>("worldPosition");
        builder.Method<&Entity::setRotationEuler>("setRotationEuler");
        builder.Method<&Entity::setScale>("setScale");
        builder.Method<&Entity::destroy>("destroy");
        builder.Method<&Entity::sceneHandle>("scene"); // entity.scene -> its bound Scene
        // send overloads (P2 messaging) - one reflected name, resolved by arg type.
        builder.Method<static_cast<void (Entity::*)(String) const>(&Entity::send)>("send");
        builder.Method<static_cast<void (Entity::*)(String, f64) const>(&Entity::send)>("send");
        builder.Method<static_cast<void (Entity::*)(String, String) const>(&Entity::send)>("send");
        builder.Method<static_cast<void (Entity::*)(String, Entity) const>(&Entity::send)>("send");
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    DRACONIC_REFLECT(Log, "draconic::script")
    {
        builder.Method<&Log::info>("info");
        builder.Method<&Log::warn>("warn");
        builder.Method<&Log::error>("error");
        builder.Constructor();
    }

    DRACONIC_REFLECT(Time, "draconic::script")
    {
        builder.Method<&Time::now>("now");
        builder.Method<&Time::delta>("delta");
        builder.Constructor();
    }

    DRACONIC_REFLECT(Random, "draconic::script")
    {
        builder.Method<&Random::value>("value");
        builder.Method<&Random::range>("range");
        builder.Method<&Random::intRange>("intRange");
        builder.Constructor();
    }

    DRACONIC_REFLECT_VALUE(Scene, "draconic::script")
    {
        builder.Method<&Scene::spawn>("spawn");
        builder.Method<&Scene::find>("find");
        builder.Method<&Scene::findByPath>("findByPath");
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    core::Span<const core::StringView> BehaviorFacadeNames()
    {
        // Kept in sync with RegisterScriptFacadeReflection below.
        static const core::StringView names[] = {
            u8"Entity", u8"Log", u8"Time", u8"Random", u8"Scene",
        };
        return core::Span<const core::StringView>{names, 5};
    }

    namespace
    {
        Array<String>& ExtraFacadeStorage()
        {
            static Array<String> names;
            return names;
        }
        Array<StringView>& ExtraFacadeViews()
        {
            static Array<StringView> views;
            return views;
        }
    }

    void RegisterExtraFacadeName(StringView name)
    {
        for (const String& existing : ExtraFacadeStorage())
        {
            if (existing.AsView() == name)
            {
                return;
            }
        } // idempotent
        ExtraFacadeStorage().PushBack(String(name));
        // Rebuild the view list from the (possibly reallocated) storage.
        Array<StringView>& views = ExtraFacadeViews();
        views.Clear();
        for (const String& n : ExtraFacadeStorage())
        {
            views.PushBack(n.AsView());
        }
    }

    Span<const StringView> ExtraFacadeNames() { return ExtraFacadeViews().AsSpan(); }

    void RegisterScriptFacadeReflection()
    {
        static const bool once = []()
        {
            DraconicRegisterValue_Entity();
            GlobalTypeRegistry().Register(TypeOf<Entity>());
            GlobalTypeRegistry().Register(Log::StaticType());
            GlobalTypeRegistry().Register(Time::StaticType());
            GlobalTypeRegistry().Register(Random::StaticType());
            DraconicRegisterValue_Scene(); // now a bound value type (mirrors Entity)
            GlobalTypeRegistry().Register(TypeOf<Scene>());
            return true;
        }();
        (void)once;
    }
}
