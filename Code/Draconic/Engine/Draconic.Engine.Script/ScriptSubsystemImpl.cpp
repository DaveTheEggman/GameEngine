// Draconic::ScriptSubsystem - implementation unit: the reflection bodies + the
// component-destroy hook. They live OUTSIDE the interface for GCC: DRACONIC_REFLECT_*
// bodies in a module interface make GCC emit an unreadable gcm cluster for
// -fno-module-lazy consumers (and a cross-partition inline virtual is not reliably
// emitted by either compiler).

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

module draconic.engine.script;

import draconic.core;
import draconic.scene;
import draconic.script;
import draconic.script.resource;

using namespace draconic::core;

namespace draconic::script
{
    // Destroying an entity (or removing the component) delivers onDestroy through the
    // scene's script system before the instances are dropped.
    void ScriptComponentManager::OnComponentDestroyed(ScriptComponent& component,
                                                      draconic::scene::EntityHandle entity)
    {
        if (m_scriptSystem != nullptr)
        {
            m_scriptSystem->ReleaseComponentInstances(component, entity);
        }
        else
        {
            for (ScriptBehavior& behavior : component.behaviors)
            {
                behavior.instance = nullptr;
                behavior.boundClass = nullptr;
            }
        }
    }

    // The component itself carries no inspector-editable reflected properties (its
    // behavior array renders through the bespoke inspector section), but it MUST be
    // reflected so the Add Component menu lists it and versioned payloads carry a
    // data version.
    DRACONIC_REFLECT_VALUE(ScriptComponent, "draconic::script")
    {
        builder.Attribute("displayName", String(u8"Script"))
            .Attribute("category", String(u8"Scripting"))
            .DataVersion(1);
    }

    void RegisterScriptComponentReflection()
    {
        static const bool once = []()
        {
            DraconicRegisterValue_ScriptComponent();
            GlobalTypeRegistry().Register(TypeOf<ScriptComponent>());
            return true;
        }();
        (void)once;
    }
}
