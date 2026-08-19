// Input - InputSubsystem implementation unit: the Input facade reflection body.
//
// Kept OUT of the interface: REFLECT_* bodies in an interface unit make GCC emit
// an unreadable gcm cluster for consumers (see gcc-module-interface-hygiene). The interface
// declares RegisterInputScriptFacade(); this unit defines it and Input::StaticType().

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module engine.input;

import foundation.core;
import foundation.script.facades; // RegisterExtraFacadeName (Input into the behavior prelude)

using namespace foundation::core;
using namespace foundation::input;

namespace engine::input
{
    REFLECT_MEMBERS(Input, "rtti::engine::input")
    {
        builder.Method<&Input::isDown>("isDown");
        builder.Method<&Input::wasPressed>("wasPressed");
        builder.Method<&Input::wasReleased>("wasReleased");
        builder.Method<&Input::value>("value");
        builder.Method<&Input::valueX>("valueX");
        builder.Method<&Input::valueY>("valueY");
        builder.Method<&Input::pushSet>("pushSet");
        builder.Method<&Input::popSet>("popSet");
        builder.Method<&Input::enableSet>("enableSet");
        // Some backends only materialize CONSTRUCTIBLE types as foreign classes.
        builder.Constructor();
    }

    void RegisterInputScriptFacade()
    {
        GlobalTypeRegistry().Register(Input::StaticType());
        // So the behavior/Level prelude imports `Input` too (AngelScript binds by
        // registry). Without this only top-level `main`/Game scripts can see it. Idempotent.
        foundation::script::RegisterExtraFacadeName(u8"Input");
    }
}
