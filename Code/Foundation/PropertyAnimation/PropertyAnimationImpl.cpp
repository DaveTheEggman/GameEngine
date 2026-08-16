// foundation.propertyanimation - the binding resolver implementation. Walks the reflected property
// chain (FindProperty per dot-segment, Nested for intermediates) and writes the leaf via reflection
// set. Kept out of the interface (uses the reflection header + is not header-inline hot).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module foundation.propertyanimation;

import foundation.core;

using namespace foundation::core;

namespace foundation::propertyanimation
{
    PropertyBinding ResolveBinding(const TypeInfo& componentType, StringView propertyPath)
    {
        PropertyBinding binding;
        if (propertyPath.IsEmpty())
        {
            return binding;
        }

        const TypeInfo* current = &componentType;
        usize start = 0;
        while (true)
        {
            usize dot = start;
            while (dot < propertyPath.Size() && propertyPath[dot] != utf8char('.'))
            {
                ++dot;
            }
            const StringView segment = propertyPath.SubStr(start, dot - start);
            if (segment.IsEmpty() || current == nullptr)
            {
                binding.chain.Clear();
                return binding;
            }
            // FindProperty keys on a C-string; property names are ASCII, so a null-terminated copy
            // of the (non-terminated) path segment reinterprets cleanly.
            const String segName(segment);
            const PropertyInfo* prop =
                FindProperty(*current, reinterpret_cast<const char*>(segName.CStr()));
            if (prop == nullptr)
            {
                binding.chain.Clear();
                return binding;
            }
            binding.chain.PushBack(prop);

            const bool isLast = (dot >= propertyPath.Size());
            if (isLast)
            {
                break;
            }
            // An intermediate segment must be a Nested struct to keep walking.
            if (!IsNested(*prop) || prop->type == nullptr)
            {
                binding.chain.Clear();
                return binding;
            }
            current = prop->type;
            start = dot + 1;
        }
        return binding;
    }

    Status WriteBinding(const PropertyBinding& binding, const Instance& componentInstance,
                        const Variant& value)
    {
        if (!binding.IsResolved() || componentInstance.IsEmpty())
        {
            return Status{ErrorCode::InvalidArgument};
        }
        // Re-walk the nested sub-instances from the LIVE component instance every call (never cache
        // sub-object pointers across structural changes - the entity.get lesson).
        Instance current = componentInstance;
        const usize count = binding.chain.Size();
        for (usize i = 0; i + 1 < count; ++i)
        {
            const PropertyInfo* prop = binding.chain[i];
            if (prop->address == nullptr)
            {
                return Status{ErrorCode::NotFound}; // computed nested property - no address to walk
            }
            void* sub = prop->address(current);
            if (sub == nullptr)
            {
                return Status{ErrorCode::NotFound}; // null nested member
            }
            current = Instance(sub, prop->type);
        }
        return SetProperty(*binding.chain[count - 1], current, value);
    }

    Variant ReadBinding(const PropertyBinding& binding, const Instance& componentInstance)
    {
        if (!binding.IsResolved() || componentInstance.IsEmpty())
        {
            return Variant{};
        }
        // Same live re-walk as WriteBinding (never cache sub-object pointers), reflection-get the leaf.
        Instance current = componentInstance;
        const usize count = binding.chain.Size();
        for (usize i = 0; i + 1 < count; ++i)
        {
            const PropertyInfo* prop = binding.chain[i];
            if (prop->address == nullptr)
            {
                return Variant{}; // computed nested property - no address to walk
            }
            void* sub = prop->address(current);
            if (sub == nullptr)
            {
                return Variant{}; // null nested member
            }
            current = Instance(sub, prop->type);
        }
        return GetProperty(*binding.chain[count - 1], current);
    }
}
