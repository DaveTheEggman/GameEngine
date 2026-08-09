// Draconic Script - :script_delegate partition
//
// IScriptDelegate: a script function held as a native callback (Traktor's
// IRuntimeDelegate). A native API takes one as RefPtr<IScriptDelegate>; holding it
// keeps the underlying script function alive (GC-safe), and Invoke() calls back
// into script with reflected Variant args, returning the script result.
//
// The seam is backend-neutral: each backend subclasses IScriptDelegate to wrap its
// own callable primitive (Wren fn handle, AngelScript function handle), and marshals
// a script function argument into one when a reflected method parameter is typed
// RefPtr<IScriptDelegate>. Because IScriptDelegate derives core::Object, the existing
// reflection object-argument marshalling (ParamTypeOf / AcceptArg / ConvertArg)
// already recognizes RefPtr<IScriptDelegate> as an object parameter - the only
// backend-specific work is wrapping the callable slot into a delegate.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.script:script_delegate;

import foundation.core;

namespace core = foundation::core;

export namespace foundation::script
{
    /// A script function exposed to native code as a typed callback. A native API takes
    /// one as `RefPtr<IScriptDelegate>`; holding the RefPtr keeps the script function
    /// alive across garbage collection. Invoke marshals `args` into the script call and
    /// returns its result (empty Variant for a void callback), or an error if the call
    /// faults or the owning context is gone.
    class IScriptDelegate : public core::Object
    {
        DRACONIC_OBJECT(IScriptDelegate, core::Object)
    public:
        [[nodiscard]] virtual core::Result<core::Variant>
        Invoke(core::Span<core::Variant> args) = 0;
    };
}

namespace foundation::script
{
    // StaticType() defined here (not a reflected registry type - scripts never construct
    // an IScriptDelegate; the type identity only needs to exist so reflected method
    // parameters can be spelled RefPtr<IScriptDelegate> and the backends can recognize it).
    DRACONIC_DEFINE_OBJECT(IScriptDelegate, "rtti::script")
}
