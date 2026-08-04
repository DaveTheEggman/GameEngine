// Draconic Core - :reflection partition (RTTI phases c-f)
//
// Reflection runtime built on Variant/Instance: properties, methods, enums'
// attributes, container reflection, and the TypeBuilder used by DRACONIC_REFLECT.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Debug/Assert.h"
#include <type_traits>
#include <utility>

export module draconic.core:reflection;

import :base;
import :allocator;
import :array;
import :span;
import :type_info;
import :variant;
import :instance;
import :object;
import :ref_counted;

// ---------------------------------------------------------------------------
// Properties (RTTI phase c)
// ---------------------------------------------------------------------------
namespace draconic::core::detail
{
    template <typename>
    struct MemberTraits;
    template <typename C, typename M>
    struct MemberTraits<M C::*>
    {
        using Class = C;
        using Member = M;
    };

    template <typename T, typename M, auto Member>
    Variant PropertyGet(const Instance& instance)
    {
        const T* object = static_cast<const T*>(instance.Pointer());
        return Variant::From<M>(object->*Member);
    }

    template <typename T, typename M, auto Member>
    Status PropertySet(const Instance& instance, const Variant& value)
    {
        const M* typed = value.TryGet<M>();
        if (typed == nullptr)
        {
            return Status{ErrorCode::InvalidArgument};
        }
        T* object = static_cast<T*>(instance.Pointer());
        object->*Member = *typed;
        return Status{};
    }

    template <typename T, typename M, auto Member>
    void* PropertyAddress(const Instance& instance)
    {
        T* object = static_cast<T*>(instance.Pointer());
        return &(object->*Member);
    }

    // A COMPUTED (getter-backed) property: the value is derived by calling a const
    // zero-arg getter that returns by value, not read from a stored field. get marshals
    // the returned value through a Variant; set is not supported (read-only); there is no
    // field address. Emits as a parens-less getter in the script backends, exactly like a
    // stored property, but the value is computed on each read.
    template <typename Getter>
    struct GetterTraits;
    template <typename C, typename R>
    struct GetterTraits<R (C::*)() const>
    {
        using Class = C;
        using Return = R;
    };
    template <typename C, typename R>
    struct GetterTraits<R (C::*)() const noexcept>
    {
        using Class = C;
        using Return = R;
    };

    template <typename T, typename R, auto Getter>
    Variant GetterPropertyGet(const Instance& instance)
    {
        const T* object = static_cast<const T*>(instance.Pointer());
        return Variant::From<R>((object->*Getter)());
    }

    template <typename T, typename R, auto Getter>
    Status GetterPropertySet(const Instance&, const Variant&)
    {
        return Status{ErrorCode::NotSupported}; // a computed getter is read-only
    }

    // A NESTED (structure) property: the member is itself a reflected type the tooling should
    // recurse INTO, not a leaf value. This is the escape hatch for members that cannot marshal
    // through a Variant - e.g. non-copyable Object members (RefCounted deletes its copy ctor).
    // get returns an EMPTY Variant and set is unsupported (the value is never passed by copy);
    // `type` + `address` are populated so a consumer reaches the nested instance in place and
    // recurses into its properties. A POINTER member resolves `address` to the POINTEE (null when
    // the member is null - consumers must null-check before recursing).
    template <typename M>
    struct NestedPointee
    {
        using Type = M;
        static constexpr bool isPointer = false;
    };
    template <typename M>
    struct NestedPointee<M*>
    {
        using Type = M;
        static constexpr bool isPointer = true;
    };

    // The canonical reflected TypeInfo for a nested type: an intrusive Object exposes it as
    // StaticType() (the patched, property-carrying one); a value type (DRACONIC_REFLECT_VALUE)
    // has no StaticType() and uses TypeOf<P>(). TypeOf<Object-type>() is a DIFFERENT, unpatched
    // TypeInfo, so the nested member's `type` must resolve through here.
    template <typename P>
    [[nodiscard]] const TypeInfo* NestedTypeInfo() noexcept
    {
        if constexpr (requires { P::StaticType(); })
        {
            return &P::StaticType();
        }
        else
        {
            return &TypeOf<P>();
        }
    }

    inline Variant NestedPropertyGet(const Instance&)
    {
        return Variant{}; // a nested structure is not read by value - recurse via address
    }
    inline Status NestedPropertySet(const Instance&, const Variant&)
    {
        return Status{ErrorCode::NotSupported}; // nor written by value
    }
    template <typename T, typename M, auto Member>
    void* NestedPropertyAddress(const Instance& instance)
    {
        T* object = static_cast<T*>(instance.Pointer());
        if constexpr (NestedPointee<M>::isPointer)
        {
            return static_cast<void*>(object->*Member); // the pointee (may be null)
        }
        else
        {
            return static_cast<void*>(&(object->*Member));
        }
    }

    [[nodiscard]] inline bool CStringEquals(const char* a, const char* b) noexcept
    {
        usize i = 0;
        while (a[i] != '\0' && a[i] == b[i])
        {
            ++i;
        }
        return a[i] == b[i];
    }
}

export namespace draconic::core
{
    enum class PropertyFlags : u32
    {
        None = 0,
        ReadOnly = 1u << 0,
        // The property is a nested reflected structure to recurse into (via `address`), not a
        // leaf value; `get` is empty and `set` unsupported. See TypeBuilder::Nested.
        Nested = 1u << 1,
    };

    struct PropertyInfo
    {
        const char* name;
        const TypeInfo* type;
        PropertyFlags flags;
        Variant (*get)(const Instance&);
        Status (*set)(const Instance&, const Variant&);
        // Raw address of the underlying field (null for computed properties). The type-erased
        // escape hatch for generic tooling: a Variant of an arbitrary reflected type (e.g. an
        // enum known only by TypeInfo) cannot be CONSTRUCTED at runtime, so editors/binding
        // generators read and write such fields in place through this instead.
        void* (*address)(const Instance&) = nullptr;
        // Per-PROPERTY attributes (tooling metadata: value ranges, conditional visibility, ...)
        // - attached fluently via TypeBuilder::PropAttribute after Property().
        const Attribute* attributes = nullptr;
        u32 attributeCount = 0;
    };

    [[nodiscard]] inline Variant GetProperty(const PropertyInfo& property, const Instance& instance)
    {
        return property.get(instance);
    }

    [[nodiscard]] inline Status SetProperty(const PropertyInfo& property, const Instance& instance,
                                            const Variant& value)
    {
        return property.set(instance, value);
    }

    // A nested structure property: recurse into `property.type`'s own properties using a
    // sub-Instance built from `property.address(instance)` (null-check it first - a null pointer
    // member yields a null address), rather than reading it as a leaf value. See TypeBuilder::Nested.
    [[nodiscard]] inline bool IsNested(const PropertyInfo& property) noexcept
    {
        return (static_cast<u32>(property.flags) & static_cast<u32>(PropertyFlags::Nested)) != 0;
    }

    // Properties declared directly on `type` (not inherited).
    [[nodiscard]] inline Span<const PropertyInfo> Properties(const TypeInfo& type) noexcept
    {
        return Span<const PropertyInfo>{type.properties, type.propertyCount};
    }

    // Count / by-index access (own properties only) for binding generators that
    // enumerate rather than search.
    [[nodiscard]] inline usize PropertyCount(const TypeInfo& type) noexcept
    {
        return type.propertyCount;
    }
    [[nodiscard]] inline const PropertyInfo& PropertyAt(const TypeInfo& type, usize index) noexcept
    {
        DRACONIC_ASSERT(index < type.propertyCount);
        return type.properties[index];
    }

    // Searches `type` and its base chain for a property by name.
    [[nodiscard]] inline const PropertyInfo* FindProperty(const TypeInfo& type,
                                                          const char* name) noexcept
    {
        for (const TypeInfo* t = &type; t != nullptr; t = t->base)
        {
            for (u32 i = 0; i < t->propertyCount; ++i)
            {
                if (detail::CStringEquals(t->properties[i].name, name))
                {
                    return &t->properties[i];
                }
            }
        }
        return nullptr;
    }

    // =======================================================================
    // Methods (RTTI phase d) - instance, const, and static, via Variant args.
    // =======================================================================
    struct ParamInfo
    {
        // Resolved lazily (a getter, not a pointer) so a type can reflect methods
        // that reference its own type without a recursive static-init.
        const TypeInfo* (*type)();
        const char* name; // optional; "" when unknown
    };

    struct MethodInfo
    {
        const char* name;
        const TypeInfo* (*returnType)(); // returns nullptr for void; lazy (see ParamInfo)
        const ParamInfo* params;
        u32 paramCount;
        bool isStatic;
        bool isConst;
        Result<Variant> (*invoke)(const Instance&, Span<Variant>);
    };

    [[nodiscard]] inline Result<Variant> InvokeMethod(const MethodInfo& method,
                                                      const Instance& instance, Span<Variant> args)
    {
        return method.invoke(instance, args);
    }

    // Convenience for static methods (no target object).
    [[nodiscard]] inline Result<Variant> InvokeStatic(const MethodInfo& method, Span<Variant> args)
    {
        return method.invoke(Instance{}, args);
    }

    [[nodiscard]] inline Span<const MethodInfo> Methods(const TypeInfo& type) noexcept
    {
        return Span<const MethodInfo>{type.methods, type.methodCount};
    }

    [[nodiscard]] inline usize MethodCount(const TypeInfo& type) noexcept
    {
        return type.methodCount;
    }
    [[nodiscard]] inline const MethodInfo& MethodAt(const TypeInfo& type, usize index) noexcept
    {
        DRACONIC_ASSERT(index < type.methodCount);
        return type.methods[index];
    }

    // A method's parameters by count / index (for binding each overload's signature).
    [[nodiscard]] inline usize ParamCount(const MethodInfo& method) noexcept
    {
        return method.paramCount;
    }
    [[nodiscard]] inline const ParamInfo& ParamAt(const MethodInfo& method, usize index) noexcept
    {
        DRACONIC_ASSERT(index < method.paramCount);
        return method.params[index];
    }

    // =======================================================================
    // Constructors - let scripting instantiate a type. invoke() validates its
    // args and returns the new instance as a Variant (a value, or object mode
    // for Object-derived types). A type may have several (overloads).
    // =======================================================================
    struct ConstructorInfo
    {
        const ParamInfo* params;
        u32 paramCount;
        Result<Variant> (*invoke)(Span<Variant> args);
    };

    [[nodiscard]] inline Span<const ConstructorInfo> Constructors(const TypeInfo& type) noexcept
    {
        return Span<const ConstructorInfo>{type.constructors, type.constructorCount};
    }

    [[nodiscard]] inline usize ConstructorCount(const TypeInfo& type) noexcept
    {
        return type.constructorCount;
    }
    [[nodiscard]] inline const ConstructorInfo& ConstructorAt(const TypeInfo& type,
                                                              usize index) noexcept
    {
        DRACONIC_ASSERT(index < type.constructorCount);
        return type.constructors[index];
    }

    // Borrows an Instance over the value/object a Variant owns (for binding
    // layers that call properties/methods on a reflected Variant).
    [[nodiscard]] inline Instance ToInstance(Variant& value) noexcept
    {
        if (value.IsObject())
        {
            return Instance(value.AsObject(), value.Type());
        }
        return Instance(value.ValuePointer(), value.Type());
    }

    // Constructs an instance by picking the constructor whose arity matches and
    // whose argument types accept `args`. Returns InvalidArgument if none match.
    [[nodiscard]] inline Result<Variant> Construct(const TypeInfo& type, Span<Variant> args)
    {
        for (u32 i = 0; i < type.constructorCount; ++i)
        {
            const ConstructorInfo& ctor = type.constructors[i];
            if (ctor.paramCount != args.Size())
            {
                continue;
            }
            Result<Variant> result = ctor.invoke(args);
            if (result.HasValue())
            {
                return result;
            }
        }
        return Err(ErrorCode::InvalidArgument);
    }

    [[nodiscard]] inline const MethodInfo* FindMethod(const TypeInfo& type,
                                                      const char* name) noexcept
    {
        for (const TypeInfo* t = &type; t != nullptr; t = t->base)
        {
            for (u32 i = 0; i < t->methodCount; ++i)
            {
                if (detail::CStringEquals(t->methods[i].name, name))
                {
                    return &t->methods[i];
                }
            }
        }
        return nullptr;
    }

    // Overload-aware lookup: matches name + exact parameter types (using the
    // ParamInfo type info each method carries). Lets several same-named methods
    // coexist and be resolved by signature.
    [[nodiscard]] inline const MethodInfo*
    FindMethod(const TypeInfo& type, const char* name,
               Span<const TypeInfo* const> paramTypes) noexcept
    {
        for (const TypeInfo* t = &type; t != nullptr; t = t->base)
        {
            for (u32 i = 0; i < t->methodCount; ++i)
            {
                const MethodInfo& method = t->methods[i];
                if (!detail::CStringEquals(method.name, name))
                {
                    continue;
                }
                if (method.paramCount != paramTypes.Size())
                {
                    continue;
                }
                bool match = true;
                for (u32 p = 0; p < method.paramCount; ++p)
                {
                    if (method.params[p].type() != paramTypes[p])
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                {
                    return &method;
                }
            }
        }
        return nullptr;
    }

    // =======================================================================
    // Attributes (phase e) - freeform key -> Variant metadata on a type.
    // =======================================================================
    struct Attribute
    {
        const char* key;
        Variant value;
    };

    [[nodiscard]] inline Span<const Attribute> Attributes(const PropertyInfo& property) noexcept
    {
        return Span<const Attribute>{property.attributes, property.attributeCount};
    }

    [[nodiscard]] inline const Attribute* FindAttribute(const PropertyInfo& property,
                                                        StringView key) noexcept
    {
        for (u32 i = 0; i < property.attributeCount; ++i)
        {
            if (StringView(reinterpret_cast<const utf8char*>(property.attributes[i].key)) == key)
            {
                return &property.attributes[i];
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline Span<const Attribute> Attributes(const TypeInfo& type) noexcept
    {
        return Span<const Attribute>{type.attributes, type.attributeCount};
    }

    [[nodiscard]] inline usize AttributeCount(const TypeInfo& type) noexcept
    {
        return type.attributeCount;
    }
    [[nodiscard]] inline const Attribute& AttributeAt(const TypeInfo& type, usize index) noexcept
    {
        DRACONIC_ASSERT(index < type.attributeCount);
        return type.attributes[index];
    }

    [[nodiscard]] inline const Variant* FindAttribute(const TypeInfo& type,
                                                      const char* key) noexcept
    {
        for (u32 i = 0; i < type.attributeCount; ++i)
        {
            if (detail::CStringEquals(type.attributes[i].key, key))
            {
                return &type.attributes[i].value;
            }
        }
        return nullptr;
    }

    // =======================================================================
    // Constants - named static values exposed for scripting (e.g. Float3::Zero,
    // Quaternion::Identity, Guid::Nil). Each holds its value as a Variant.
    // =======================================================================
    struct ConstantInfo
    {
        const char* name;
        const TypeInfo* type;
        Variant value;
    };

    [[nodiscard]] inline Span<const ConstantInfo> Constants(const TypeInfo& type) noexcept
    {
        return Span<const ConstantInfo>{type.constants, type.constantCount};
    }

    [[nodiscard]] inline usize ConstantCount(const TypeInfo& type) noexcept
    {
        return type.constantCount;
    }
    [[nodiscard]] inline const ConstantInfo& ConstantAt(const TypeInfo& type, usize index) noexcept
    {
        DRACONIC_ASSERT(index < type.constantCount);
        return type.constants[index];
    }

    [[nodiscard]] inline const ConstantInfo* FindConstant(const TypeInfo& type,
                                                          const char* name) noexcept
    {
        for (u32 i = 0; i < type.constantCount; ++i)
        {
            if (detail::CStringEquals(type.constants[i].name, name))
            {
                return &type.constants[i];
            }
        }
        return nullptr;
    }

    // =======================================================================
    // Container reflection (phase f) - generic indexed access to Array<T>, so
    // tools/scripting can iterate without knowing the element type statically.
    // =======================================================================
    struct ContainerInfo
    {
        const TypeInfo* elementType;
        usize (*size)(const Instance&);
        Variant (*getAt)(const Instance&, usize index);
        Status (*setAt)(const Instance&, usize index, const Variant& value);
    };

    [[nodiscard]] inline bool IsContainer(const TypeInfo& type) noexcept
    {
        return type.container != nullptr;
    }

    [[nodiscard]] inline usize ContainerSize(const ContainerInfo& container,
                                             const Instance& instance)
    {
        return container.size(instance);
    }

    [[nodiscard]] inline Variant ContainerGetAt(const ContainerInfo& container,
                                                const Instance& instance, usize index)
    {
        return container.getAt(instance, index);
    }

    inline Status ContainerSetAt(const ContainerInfo& container, const Instance& instance,
                                 usize index, const Variant& value)
    {
        return container.setAt(instance, index, value);
    }

    // Registers Array<T> as a reflected container (patches TypeOf<Array<T>>()).
    template <typename T>
    void RegisterArrayType()
    {
        static const ContainerInfo info{
            &TypeOf<T>(), [](const Instance& i) -> usize
            { return static_cast<const Array<T>*>(i.Pointer())->Size(); },
            [](const Instance& i, usize index) -> Variant
            { return Variant::From<T>((*static_cast<const Array<T>*>(i.Pointer()))[index]); },
            [](const Instance& i, usize index, const Variant& value) -> Status
            {
                const T* typed = value.TryGet<T>();
                if (typed == nullptr)
                {
                    return Status{ErrorCode::InvalidArgument};
                }
                (*static_cast<Array<T>*>(i.Pointer()))[index] = *typed;
                return Status{};
            }};
        const_cast<TypeInfo&>(TypeOf<Array<T>>()).container = &info;
    }
}

namespace draconic::core::detail
{
    // Object-argument support: a parameter A may be a value type, or an object
    // form (RefPtr<U>, U*, or U&/const U& with U deriving Object). Object args
    // are extracted from an object-mode Variant via AsObject<U>().
    template <typename T>
    struct ArgRefPtr
    {
        static constexpr bool value = false;
    };
    template <typename U>
    struct ArgRefPtr<RefPtr<U>>
    {
        static constexpr bool value = true;
        using Pointee = U;
    };

    template <typename A>
    [[nodiscard]] const TypeInfo* ParamTypeOf() noexcept
    {
        using Bare = std::remove_cvref_t<A>;
        if constexpr (ArgRefPtr<Bare>::value)
        {
            return &ArgRefPtr<Bare>::Pointee::StaticType();
        }
        else if constexpr (std::is_pointer_v<Bare> &&
                           std::is_base_of_v<Object, std::remove_cv_t<std::remove_pointer_t<Bare>>>)
        {
            return &std::remove_cv_t<std::remove_pointer_t<Bare>>::StaticType();
        }
        else if constexpr (std::is_class_v<Bare> && std::is_base_of_v<Object, Bare>)
        {
            return &Bare::StaticType();
        }
        else
        {
            return &TypeOf<Bare>();
        }
    }

    template <typename A>
    [[nodiscard]] bool AcceptArg(const Variant& v) noexcept
    {
        using Bare = std::remove_cvref_t<A>;
        if constexpr (ArgRefPtr<Bare>::value)
        {
            using U = typename ArgRefPtr<Bare>::Pointee;
            return v.IsObject() && (v.AsObject() == nullptr || v.AsObject<U>() != nullptr);
        }
        else if constexpr (std::is_pointer_v<Bare> &&
                           std::is_base_of_v<Object, std::remove_cv_t<std::remove_pointer_t<Bare>>>)
        {
            using U = std::remove_cv_t<std::remove_pointer_t<Bare>>;
            return v.IsObject() && (v.AsObject() == nullptr || v.AsObject<U>() != nullptr);
        }
        else if constexpr (std::is_class_v<Bare> && std::is_base_of_v<Object, Bare>)
        {
            return v.IsObject() && v.AsObject<Bare>() != nullptr; // reference: must be non-null
        }
        else
        {
            return v.TryGet<Bare>() != nullptr;
        }
    }

    template <typename A>
    [[nodiscard]] decltype(auto) ConvertArg(Variant& v) noexcept
    {
        using Bare = std::remove_cvref_t<A>;
        if constexpr (ArgRefPtr<Bare>::value)
        {
            using U = typename ArgRefPtr<Bare>::Pointee;
            return RefPtr<U>(v.AsObject<U>());
        }
        else if constexpr (std::is_pointer_v<Bare> &&
                           std::is_base_of_v<Object, std::remove_cv_t<std::remove_pointer_t<Bare>>>)
        {
            using U = std::remove_cv_t<std::remove_pointer_t<Bare>>;
            return v.AsObject<U>();
        }
        else if constexpr (std::is_class_v<Bare> && std::is_base_of_v<Object, Bare>)
        {
            return *v.AsObject<Bare>();
        }
        else
        {
            return *v.template TryGet<Bare>();
        }
    }

    template <typename... A>
    [[nodiscard]] Span<const ParamInfo> MakeParams()
    {
        if constexpr (sizeof...(A) == 0)
        {
            return Span<const ParamInfo>{};
        }
        else
        {
            static const ParamInfo params[] = {ParamInfo{&ParamTypeOf<A>, ""}...};
            return Span<const ParamInfo>{params, sizeof...(A)};
        }
    }

    template <typename... A, usize... I>
    [[nodiscard]] bool ArgsMatch(Span<Variant>& args, std::index_sequence<I...>)
    {
        return (... && AcceptArg<A>(args[I]));
    }

    template <auto Member, typename C, typename R, bool Const, typename... A, usize... I>
    Result<Variant> InvokeMemberImpl(const Instance& instance, Span<Variant> args,
                                     std::index_sequence<I...> seq)
    {
        if (args.Size() != sizeof...(A))
        {
            return Err(ErrorCode::InvalidArgument);
        }
        if constexpr (sizeof...(A) > 0)
        {
            if (!ArgsMatch<A...>(args, seq))
            {
                return Err(ErrorCode::InvalidArgument);
            }
        }
        using ObjectType = std::conditional_t<Const, const C, C>;
        ObjectType* object = static_cast<ObjectType*>(instance.Pointer());
        if constexpr (std::is_void_v<R>)
        {
            (object->*Member)(ConvertArg<A>(args[I])...);
            return Variant{};
        }
        else
        {
            return Variant::From<std::remove_cvref_t<R>>(
                (object->*Member)(ConvertArg<A>(args[I])...));
        }
    }

    template <auto Func, typename R, typename... A, usize... I>
    Result<Variant> InvokeFreeImpl(Span<Variant> args, std::index_sequence<I...> seq)
    {
        if (args.Size() != sizeof...(A))
        {
            return Err(ErrorCode::InvalidArgument);
        }
        if constexpr (sizeof...(A) > 0)
        {
            if (!ArgsMatch<A...>(args, seq))
            {
                return Err(ErrorCode::InvalidArgument);
            }
        }
        if constexpr (std::is_void_v<R>)
        {
            Func(ConvertArg<A>(args[I])...);
            return Variant{};
        }
        else
        {
            return Variant::From<std::remove_cvref_t<R>>(Func(ConvertArg<A>(args[I])...));
        }
    }

    template <typename R>
    [[nodiscard]] const TypeInfo* ReturnTypeInfo() noexcept
    {
        if constexpr (std::is_void_v<R>)
        {
            return nullptr;
        }
        else
        {
            return ParamTypeOf<R>();
        } // object-aware (StaticType for objects)
    }

    template <typename T, typename... A, usize... I>
    Result<Variant> ConstructImpl(Span<Variant> args, std::index_sequence<I...> seq)
    {
        if (args.Size() != sizeof...(A))
        {
            return Err(ErrorCode::InvalidArgument);
        }
        if constexpr (sizeof...(A) > 0)
        {
            if (!ArgsMatch<A...>(args, seq))
            {
                return Err(ErrorCode::InvalidArgument);
            }
        }
        if constexpr (std::is_base_of_v<Object, T>)
        {
            // Object-derived: heap-allocate via MakeRef -> Variant object mode.
            return Variant::From(MakeRef<T>(DefaultAllocator(), ConvertArg<A>(args[I])...));
        }
        else
        {
            return Variant::From<T>(T(ConvertArg<A>(args[I])...));
        }
    }

    template <typename T, typename... A>
    struct ConstructorReflect
    {
        static Span<const ParamInfo> Params() { return MakeParams<A...>(); }
        static Result<Variant> Invoke(Span<Variant> args)
        {
            return ConstructImpl<T, A...>(args, std::index_sequence_for<A...>{});
        }
    };

    template <auto Member, typename Sig = decltype(Member)>
    struct MethodReflect;

    template <auto Member, typename C, typename R, typename... A> // instance method
    struct MethodReflect<Member, R (C::*)(A...)>
    {
        static constexpr bool isStatic = false;
        static constexpr bool isConst = false;
        static const TypeInfo* ReturnType() { return ReturnTypeInfo<R>(); }
        static Span<const ParamInfo> Params() { return MakeParams<A...>(); }
        static Result<Variant> Invoke(const Instance& i, Span<Variant> a)
        {
            return InvokeMemberImpl<Member, C, R, false, A...>(i, a,
                                                               std::index_sequence_for<A...>{});
        }
    };

    template <auto Member, typename C, typename R, typename... A> // const instance method
    struct MethodReflect<Member, R (C::*)(A...) const>
    {
        static constexpr bool isStatic = false;
        static constexpr bool isConst = true;
        static const TypeInfo* ReturnType() { return ReturnTypeInfo<R>(); }
        static Span<const ParamInfo> Params() { return MakeParams<A...>(); }
        static Result<Variant> Invoke(const Instance& i, Span<Variant> a)
        {
            return InvokeMemberImpl<Member, C, R, true, A...>(i, a,
                                                              std::index_sequence_for<A...>{});
        }
    };

    template <auto Func, typename R, typename... A> // static / free function
    struct MethodReflect<Func, R (*)(A...)>
    {
        static constexpr bool isStatic = true;
        static constexpr bool isConst = false;
        static const TypeInfo* ReturnType() { return ReturnTypeInfo<R>(); }
        static Span<const ParamInfo> Params() { return MakeParams<A...>(); }
        static Result<Variant> Invoke(const Instance&, Span<Variant> a)
        {
            return InvokeFreeImpl<Func, R, A...>(a, std::index_sequence_for<A...>{});
        }
    };

    // noexcept is part of the function type (C++17), so each form needs a
    // noexcept twin. These delegate to the same invoke implementations.
    template <auto Member, typename C, typename R, typename... A> // instance method (noexcept)
    struct MethodReflect<Member, R (C::*)(A...) noexcept>
    {
        static constexpr bool isStatic = false;
        static constexpr bool isConst = false;
        static const TypeInfo* ReturnType() { return ReturnTypeInfo<R>(); }
        static Span<const ParamInfo> Params() { return MakeParams<A...>(); }
        static Result<Variant> Invoke(const Instance& i, Span<Variant> a)
        {
            return InvokeMemberImpl<Member, C, R, false, A...>(i, a,
                                                               std::index_sequence_for<A...>{});
        }
    };

    template <auto Member, typename C, typename R,
              typename... A> // const instance method (noexcept)
    struct MethodReflect<Member, R (C::*)(A...) const noexcept>
    {
        static constexpr bool isStatic = false;
        static constexpr bool isConst = true;
        static const TypeInfo* ReturnType() { return ReturnTypeInfo<R>(); }
        static Span<const ParamInfo> Params() { return MakeParams<A...>(); }
        static Result<Variant> Invoke(const Instance& i, Span<Variant> a)
        {
            return InvokeMemberImpl<Member, C, R, true, A...>(i, a,
                                                              std::index_sequence_for<A...>{});
        }
    };

    template <auto Func, typename R, typename... A> // static / free function (noexcept)
    struct MethodReflect<Func, R (*)(A...) noexcept>
    {
        static constexpr bool isStatic = true;
        static constexpr bool isConst = false;
        static const TypeInfo* ReturnType() { return ReturnTypeInfo<R>(); }
        static Span<const ParamInfo> Params() { return MakeParams<A...>(); }
        static Result<Variant> Invoke(const Instance&, Span<Variant> a)
        {
            return InvokeFreeImpl<Func, R, A...>(a, std::index_sequence_for<A...>{});
        }
    };
}

export namespace draconic::core
{
    // Holds a type's TypeInfo together with the property/method arrays it points
    // into. Stored as a single static (see DRACONIC_REFLECT); Array's move
    // preserves the buffer address, so the TypeInfo pointers stay valid.
    struct TypeData
    {
        Array<PropertyInfo> properties;
        Array<Array<Attribute>> propertyAttributes; // parallel to `properties`
        Array<MethodInfo> methods;
        Array<Attribute> attributes;
        Array<ConstantInfo> constants;
        Array<ConstructorInfo> constructors;
        TypeInfo info{};
    };

    template <typename T>
    class TypeBuilder
    {
    public:
        TypeBuilder(const char* name, const char* namespaceName, const TypeInfo* base) noexcept
            : m_name(name), m_namespace(namespaceName), m_base(base)
        {
        }

        /// Serialization data version (migration): bump when the serialized layout changes.
        TypeBuilder& DataVersion(u32 version)
        {
            m_dataVersion = version;
            return *this;
        }

        template <auto Member>
        TypeBuilder& Property(const char* name, PropertyFlags flags = PropertyFlags::None)
        {
            using M = typename detail::MemberTraits<decltype(Member)>::Member;
            m_data.properties.PushBack(PropertyInfo{
                name, &TypeOf<M>(), flags, &detail::PropertyGet<T, M, Member>,
                &detail::PropertySet<T, M, Member>, &detail::PropertyAddress<T, M, Member>});
            return *this;
        }

        /// A COMPUTED read-only property backed by a const zero-arg getter (returns by value).
        /// Emits as a parens-less getter in the script backends and reflects as a property to
        /// tooling, but the value is derived rather than a stored field: `address` is null (no
        /// in-place editing) and set returns NotSupported. Use for facade accessors that should
        /// read as a property (`entity.scene`) rather than a method (`entity.scene()`).
        template <auto Getter>
        TypeBuilder& ComputedProperty(const char* name)
        {
            using R = typename detail::GetterTraits<decltype(Getter)>::Return;
            m_data.properties.PushBack(PropertyInfo{name, &TypeOf<R>(), PropertyFlags::ReadOnly,
                                                    &detail::GetterPropertyGet<T, R, Getter>,
                                                    &detail::GetterPropertySet<T, R, Getter>,
                                                    nullptr});
            return *this;
        }

        /// A NESTED structure property: `member` is itself a reflected type the tooling recurses
        /// into (a `MaterialSource source;` value member, or a `MeshSource* source;` pointer
        /// member). Unlike Property<>, it never copies the member through a Variant, so it works
        /// for non-copyable Object members. `get` is empty, `set` returns NotSupported, `type` is
        /// the nested type, and `address` yields the member (the POINTEE for a pointer member, null
        /// when unset). Consumers check IsNested(), then recurse via address; script harvest skips it.
        template <auto Member>
        TypeBuilder& Nested(const char* name)
        {
            using M = typename detail::MemberTraits<decltype(Member)>::Member;
            using Pointee = typename detail::NestedPointee<M>::Type;
            m_data.properties.PushBack(PropertyInfo{name, detail::NestedTypeInfo<Pointee>(),
                                                    PropertyFlags::Nested,
                                                    &detail::NestedPropertyGet,
                                                    &detail::NestedPropertySet,
                                                    &detail::NestedPropertyAddress<T, M, Member>});
            return *this;
        }

        template <auto Member>
        TypeBuilder& Method(const char* name)
        {
            using Reflect = detail::MethodReflect<Member>;
            const Span<const ParamInfo> params = Reflect::Params();
            m_data.methods.PushBack(MethodInfo{name, &Reflect::ReturnType, params.Data(),
                                               static_cast<u32>(params.Size()), Reflect::isStatic,
                                               Reflect::isConst, &Reflect::Invoke});
            return *this;
        }

        template <typename V>
        TypeBuilder& Attribute(const char* key, V value)
        {
            m_data.attributes.PushBack(
                draconic::core::Attribute{key, Variant::From<V>(Move(value))});
            return *this;
        }

        /// Attach an attribute to the MOST RECENTLY added property (fluent - call right
        /// after Property()). Tooling metadata: "range" (Float4 min/max/step), "visibleWhen"
        /// (String "prop" truthy or "prop=1,2" value list), etc.
        template <typename V>
        TypeBuilder& PropAttribute(const char* key, V value)
        {
            if (m_data.properties.IsEmpty())
            {
                return *this;
            }
            while (m_data.propertyAttributes.Size() < m_data.properties.Size())
            {
                m_data.propertyAttributes.PushBack(Array<draconic::core::Attribute>{});
            }
            m_data.propertyAttributes[m_data.properties.Size() - 1].PushBack(
                draconic::core::Attribute{key, Variant::From<V>(Move(value))});
            return *this;
        }

        template <typename V>
        TypeBuilder& Constant(const char* name, V value)
        {
            m_data.constants.PushBack(
                draconic::core::ConstantInfo{name, &TypeOf<V>(), Variant::From<V>(Move(value))});
            return *this;
        }

        // Reflects a constructor T(Args...). Call multiple times for overloads.
        template <typename... Args>
        TypeBuilder& Constructor()
        {
            using Reflect = detail::ConstructorReflect<T, Args...>;
            const Span<const ParamInfo> params = Reflect::Params();
            m_data.constructors.PushBack(
                ConstructorInfo{params.Data(), static_cast<u32>(params.Size()), &Reflect::Invoke});
            return *this;
        }

        [[nodiscard]] TypeData Build()
        {
            m_data.info = MakeTypeInfo<T>(m_name, m_namespace, m_base, m_dataVersion);
            // Wire per-property attribute spans (the inner Array buffers survive TypeData's
            // move - only the outer array's control block moves).
            while (m_data.propertyAttributes.Size() < m_data.properties.Size())
            {
                m_data.propertyAttributes.PushBack(Array<draconic::core::Attribute>{});
            }
            for (usize i = 0; i < m_data.properties.Size(); ++i)
            {
                m_data.properties[i].attributes = m_data.propertyAttributes[i].Data();
                m_data.properties[i].attributeCount =
                    static_cast<u32>(m_data.propertyAttributes[i].Size());
            }
            m_data.info.properties = m_data.properties.Data();
            m_data.info.propertyCount = static_cast<u32>(m_data.properties.Size());
            m_data.info.methods = m_data.methods.Data();
            m_data.info.methodCount = static_cast<u32>(m_data.methods.Size());
            m_data.info.attributes = m_data.attributes.Data();
            m_data.info.attributeCount = static_cast<u32>(m_data.attributes.Size());
            m_data.info.constants = m_data.constants.Data();
            m_data.info.constantCount = static_cast<u32>(m_data.constants.Size());
            m_data.info.constructors = m_data.constructors.Data();
            m_data.info.constructorCount = static_cast<u32>(m_data.constructors.Size());
            return Move(m_data);
        }

    private:
        const char* m_name;
        const char* m_namespace;
        const TypeInfo* m_base;
        u32 m_dataVersion = 0;
        TypeData m_data;
    };
}
