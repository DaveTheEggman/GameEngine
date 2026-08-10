// Foundation::Script.Facades - implementation unit: the REFLECT_* bodies (they
// never sit in a module interface unit - the GCC gcm-cluster rule).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Core/Log/Log.h"

module foundation.script.facades;

import foundation.core;
import foundation.scene;
import foundation.script;

using namespace foundation::core;
namespace core = foundation::core;

namespace foundation::script
{
    REFLECT_VALUE(Entity, "rtti::script")
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
        // A computed property (parens-less): `entity.scene` reads its bound Scene. Not a
        // method, so scripts write `entity.scene.find(...)` without call parens.
        builder.ComputedProperty<&Entity::sceneHandle>("scene");
        // send (P2 messaging) - ONE conceptual method as an ARITY FAMILY: send(name) and
        // send(name, payload:Variant), dispatched by argument count. The Variant carries any script
        // value onto the StringHash+Variant bus (the honest payload type).
        builder.Method<static_cast<void (Entity::*)(String) const>(&Entity::send)>("send");
        builder.Method<static_cast<void (Entity::*)(String, Variant) const>(&Entity::send)>(
            "send", {"name", "payload"});
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    REFLECT_MEMBERS(Log, "rtti::script")
    {
        builder.Method<&Log::info>("info");
        builder.Method<&Log::warn>("warn");
        builder.Method<&Log::error>("error");
        builder.Constructor();
    }

    REFLECT_MEMBERS(Time, "rtti::script")
    {
        builder.Method<&Time::now>("now");
        builder.Method<&Time::delta>("delta");
        builder.Constructor();
    }

    REFLECT_MEMBERS(Random, "rtti::script")
    {
        builder.Method<&Random::value>("value");
        builder.Method<&Random::range>("range");
        builder.Method<&Random::intRange>("intRange");
        builder.Constructor();
    }

    REFLECT_VALUE(Scene, "rtti::script")
    {
        builder.Method<&Scene::spawn>("spawn");
        builder.Method<&Scene::find>("find");
        builder.Method<&Scene::findByPath>("findByPath");
        // A computed property (parens-less): `scene.events` reads this scene's event-bus handle.
        builder.ComputedProperty<&Scene::eventsHandle>("events");
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    REFLECT_VALUE(SceneEvents, "rtti::script")
    {
        // emit - ONE conceptual method as an ARITY FAMILY (mirrors Entity::send): emit(name) and
        // emit(name, payload:Variant), dispatched by argument count; the Variant carries any value.
        builder.Method<static_cast<void (SceneEvents::*)(String) const>(&SceneEvents::emit)>("emit");
        builder.Method<static_cast<void (SceneEvents::*)(String, Variant) const>(&SceneEvents::emit)>(
            "emit", {"name", "payload"});
        builder.Constructor(); // Wren only materializes constructible foreign classes
    }

    core::Span<const core::StringView> BehaviorFacadeNames()
    {
        // Kept in sync with RegisterScriptFacadeReflection below.
        static const core::StringView names[] = {
            u8"Entity", u8"Log", u8"Time", u8"Random", u8"Scene", u8"SceneEvents",
        };
        return core::Span<const core::StringView>{names, 6};
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
        Array<const core::TypeInfo*>& ExtraRootStorage()
        {
            static Array<const core::TypeInfo*> roots;
            return roots;
        }
    }

    void RegisterExtraScriptRootType(const core::TypeInfo* type)
    {
        if (type == nullptr)
        {
            return;
        }
        for (const core::TypeInfo* existing : ExtraRootStorage())
        {
            if (existing == type)
            {
                return; // idempotent
            }
        }
        ExtraRootStorage().PushBack(type);
    }

    core::Span<const core::TypeInfo* const> ExtraScriptRootTypes()
    {
        return {ExtraRootStorage().Data(), ExtraRootStorage().Size()};
    }

    void RegisterExtraFacadeName(StringView name)
    {
        // Reserved contract-class names: a user's own script class MUST take these - the game
        // orchestrator is class `Game` (StartScript does CreateInstance("Game")), and the scene
        // tier is class `Level` (instantiated once per scene). A facade sharing either name would
        // clash (AngelScript "Name conflict", Wren "import ... for <name>") and the user's class
        // could not compile. Refuse the registration. See docs/design/adding-facades.md.
        const StringView kReservedNames[] = {StringView(u8"Game"), StringView(u8"Level")};
        for (StringView reserved : kReservedNames)
        {
            if (name == reserved)
            {
                LOG_ERROR(
                    u8"Script",
                    u8"facade name '{}' is reserved for a user contract class - registration refused",
                    name);
                return; // refuse: the name is never added, so the user's class always wins

            }
        }
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
            RttiRegisterValue_Entity();
            GlobalTypeRegistry().Register(TypeOf<Entity>());
            GlobalTypeRegistry().Register(Log::StaticType());
            GlobalTypeRegistry().Register(Time::StaticType());
            GlobalTypeRegistry().Register(Random::StaticType());
            RttiRegisterValue_Scene(); // now a bound value type (mirrors Entity)
            GlobalTypeRegistry().Register(TypeOf<Scene>());
            RttiRegisterValue_SceneEvents(); // the scene.events handle (mirrors Scene)
            GlobalTypeRegistry().Register(TypeOf<SceneEvents>());
            return true;
        }();
        (void)once;
    }
}
