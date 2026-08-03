// Draconic Input - InputSubsystem implementation unit: the Input facade reflection body.
//
// Kept OUT of the interface: DRACONIC_REFLECT_* bodies in an interface unit make GCC emit
// an unreadable gcm cluster for consumers (see gcc-module-interface-hygiene). The interface
// declares RegisterInputScriptApi(); this unit defines it and Input::StaticType().

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

module draconic.engine.input;

import draconic.core;
import draconic.script.facades; // RegisterExtraFacadeName (Input into the behavior prelude)

using namespace draconic::core;

namespace draconic::input
{
    DRACONIC_REFLECT(Input, "draconic::input")
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
        // The Wren emitter only materializes CONSTRUCTIBLE types as foreign classes.
        builder.Constructor();
    }

    void RegisterInputScriptApi()
    {
        GlobalTypeRegistry().Register(Input::StaticType());
        // So the Wren behavior/Level prelude imports `Input` too (AngelScript binds by
        // registry). Without this only top-level `main`/Game scripts can see it. Idempotent.
        draconic::script::RegisterExtraFacadeName(u8"Input");
    }
}
