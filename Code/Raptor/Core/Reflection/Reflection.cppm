// Raptor Core — :reflection partition (RTTI phases c-f)
//
// Reflection runtime built on Variant/Instance: properties, methods, enums'
// attributes, container reflection, and the TypeBuilder used by RAPTOR_REFLECT.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <type_traits>
#include <utility>

export module raptor.core:reflection;

import :base;
import :allocator;
import :array;
import :span;
import :type_info;
import :variant;
import :instance;

// ---------------------------------------------------------------------------
// Properties (RTTI phase c)
// ---------------------------------------------------------------------------
namespace raptor::core::detail
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
            return Status{ ErrorCode::InvalidArgument };
        }
        T* object = static_cast<T*>(instance.Pointer());
        object->*Member = *typed;
        return Status{};
    }

    [[nodiscard]] inline bool CStringEquals(const char* a, const char* b) noexcept
    {
        usize i = 0;
        while (a[i] != '\0' && a[i] == b[i]) { ++i; }
        return a[i] == b[i];
    }
}

export namespace raptor::core
{
    enum class PropertyFlags : u32
    {
        None = 0,
        ReadOnly = 1u << 0,
    };

    struct PropertyInfo
    {
        const char* name;
        const TypeInfo* type;
        PropertyFlags flags;
        Variant (*get)(const Instance&);
        Status (*set)(const Instance&, const Variant&);
    };

    [[nodiscard]] inline Variant GetProperty(const PropertyInfo& property, const Instance& instance)
    {
        return property.get(instance);
    }

    [[nodiscard]] inline Status SetProperty(const PropertyInfo& property, const Instance& instance, const Variant& value)
    {
        return property.set(instance, value);
    }

    // Properties declared directly on `type` (not inherited).
    [[nodiscard]] inline Span<const PropertyInfo> Properties(const TypeInfo& type) noexcept
    {
        return Span<const PropertyInfo>{ type.properties, type.propertyCount };
    }

    // Searches `type` and its base chain for a property by name.
    [[nodiscard]] inline const PropertyInfo* FindProperty(const TypeInfo& type, const char* name) noexcept
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
    // Methods (RTTI phase d) — instance, const, and static, via Variant args.
    // =======================================================================
    struct ParamInfo
    {
        const TypeInfo* type;
        const char* name; // optional; "" when unknown
    };

    struct MethodInfo
    {
        const char* name;
        const TypeInfo* returnType; // nullptr for void
        const ParamInfo* params;
        u32 paramCount;
        bool isStatic;
        bool isConst;
        Result<Variant> (*invoke)(const Instance&, Span<Variant>);
    };

    [[nodiscard]] inline Result<Variant> InvokeMethod(const MethodInfo& method, const Instance& instance, Span<Variant> args)
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
        return Span<const MethodInfo>{ type.methods, type.methodCount };
    }

    [[nodiscard]] inline const MethodInfo* FindMethod(const TypeInfo& type, const char* name) noexcept
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

    // =======================================================================
    // Attributes (phase e) — freeform key -> Variant metadata on a type.
    // =======================================================================
    struct Attribute
    {
        const char* key;
        Variant value;
    };

    [[nodiscard]] inline Span<const Attribute> Attributes(const TypeInfo& type) noexcept
    {
        return Span<const Attribute>{ type.attributes, type.attributeCount };
    }

    [[nodiscard]] inline const Variant* FindAttribute(const TypeInfo& type, const char* key) noexcept
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
    // Constants — named static values exposed for scripting (e.g. Vec3::Zero,
    // Quat::Identity, Guid::Nil). Each holds its value as a Variant.
    // =======================================================================
    struct ConstantInfo
    {
        const char* name;
        const TypeInfo* type;
        Variant value;
    };

    [[nodiscard]] inline Span<const ConstantInfo> Constants(const TypeInfo& type) noexcept
    {
        return Span<const ConstantInfo>{ type.constants, type.constantCount };
    }

    [[nodiscard]] inline const ConstantInfo* FindConstant(const TypeInfo& type, const char* name) noexcept
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
    // Container reflection (phase f) — generic indexed access to Array<T>, so
    // tools/scripting can iterate without knowing the element type statically.
    // =======================================================================
    struct ContainerInfo
    {
        const TypeInfo* elementType;
        usize (*size)(const Instance&);
        Variant (*getAt)(const Instance&, usize index);
        Status (*setAt)(const Instance&, usize index, const Variant& value);
    };

    [[nodiscard]] inline bool IsContainer(const TypeInfo& type) noexcept { return type.container != nullptr; }

    [[nodiscard]] inline usize ContainerSize(const ContainerInfo& container, const Instance& instance)
    {
        return container.size(instance);
    }

    [[nodiscard]] inline Variant ContainerGetAt(const ContainerInfo& container, const Instance& instance, usize index)
    {
        return container.getAt(instance, index);
    }

    inline Status ContainerSetAt(const ContainerInfo& container, const Instance& instance, usize index, const Variant& value)
    {
        return container.setAt(instance, index, value);
    }

    // Registers Array<T> as a reflected container (patches TypeOf<Array<T>>()).
    template <typename T>
    void RegisterArrayType()
    {
        static const ContainerInfo info{
            &TypeOf<T>(),
            [](const Instance& i) -> usize { return static_cast<const Array<T>*>(i.Pointer())->Size(); },
            [](const Instance& i, usize index) -> Variant
            { return Variant::From<T>((*static_cast<const Array<T>*>(i.Pointer()))[index]); },
            [](const Instance& i, usize index, const Variant& value) -> Status
            {
                const T* typed = value.TryGet<T>();
                if (typed == nullptr) { return Status{ ErrorCode::InvalidArgument }; }
                (*static_cast<Array<T>*>(i.Pointer()))[index] = *typed;
                return Status{};
            }
        };
        const_cast<TypeInfo&>(TypeOf<Array<T>>()).container = &info;
    }
}

namespace raptor::core::detail
{
    template <typename... A>
    [[nodiscard]] Span<const ParamInfo> MakeParams()
    {
        if constexpr (sizeof...(A) == 0)
        {
            return Span<const ParamInfo>{};
        }
        else
        {
            static const ParamInfo params[] = { ParamInfo{ &TypeOf<std::remove_cvref_t<A>>(), "" }... };
            return Span<const ParamInfo>{ params, sizeof...(A) };
        }
    }

    template <typename... A, usize... I>
    [[nodiscard]] bool ArgsMatch(Span<Variant>& args, std::index_sequence<I...>)
    {
        return ( ... && (args[I].template TryGet<std::remove_cvref_t<A>>() != nullptr) );
    }

    template <auto Member, typename C, typename R, bool Const, typename... A, usize... I>
    Result<Variant> InvokeMemberImpl(const Instance& instance, Span<Variant> args, std::index_sequence<I...> seq)
    {
        if (args.Size() != sizeof...(A)) { return Err(ErrorCode::InvalidArgument); }
        if constexpr (sizeof...(A) > 0)
        {
            if (!ArgsMatch<A...>(args, seq)) { return Err(ErrorCode::InvalidArgument); }
        }
        using ObjectType = std::conditional_t<Const, const C, C>;
        ObjectType* object = static_cast<ObjectType*>(instance.Pointer());
        if constexpr (std::is_void_v<R>)
        {
            (object->*Member)(*args[I].template TryGet<std::remove_cvref_t<A>>()...);
            return Variant{};
        }
        else
        {
            return Variant::From<std::remove_cvref_t<R>>(
                (object->*Member)(*args[I].template TryGet<std::remove_cvref_t<A>>()...));
        }
    }

    template <auto Func, typename R, typename... A, usize... I>
    Result<Variant> InvokeFreeImpl(Span<Variant> args, std::index_sequence<I...> seq)
    {
        if (args.Size() != sizeof...(A)) { return Err(ErrorCode::InvalidArgument); }
        if constexpr (sizeof...(A) > 0)
        {
            if (!ArgsMatch<A...>(args, seq)) { return Err(ErrorCode::InvalidArgument); }
        }
        if constexpr (std::is_void_v<R>)
        {
            Func(*args[I].template TryGet<std::remove_cvref_t<A>>()...);
            return Variant{};
        }
        else
        {
            return Variant::From<std::remove_cvref_t<R>>(Func(*args[I].template TryGet<std::remove_cvref_t<A>>()...));
        }
    }

    template <typename R>
    [[nodiscard]] const TypeInfo* ReturnTypeInfo() noexcept
    {
        if constexpr (std::is_void_v<R>) { return nullptr; }
        else { return &TypeOf<std::remove_cvref_t<R>>(); }
    }

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
            return InvokeMemberImpl<Member, C, R, false, A...>(i, a, std::index_sequence_for<A...>{});
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
            return InvokeMemberImpl<Member, C, R, true, A...>(i, a, std::index_sequence_for<A...>{});
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
}

export namespace raptor::core
{
    // Holds a type's TypeInfo together with the property/method arrays it points
    // into. Stored as a single static (see RAPTOR_REFLECT); Array's move
    // preserves the buffer address, so the TypeInfo pointers stay valid.
    struct TypeData
    {
        Array<PropertyInfo> properties;
        Array<MethodInfo> methods;
        Array<Attribute> attributes;
        Array<ConstantInfo> constants;
        TypeInfo info{};
    };

    template <typename T>
    class TypeBuilder
    {
    public:
        TypeBuilder(const char* name, const char* namespaceName, const TypeInfo* base) noexcept
            : m_name(name), m_namespace(namespaceName), m_base(base) {}

        template <auto Member>
        TypeBuilder& Property(const char* name, PropertyFlags flags = PropertyFlags::None)
        {
            using M = typename detail::MemberTraits<decltype(Member)>::Member;
            m_data.properties.PushBack(PropertyInfo{
                name, &TypeOf<M>(), flags,
                &detail::PropertyGet<T, M, Member>,
                &detail::PropertySet<T, M, Member> });
            return *this;
        }

        template <auto Member>
        TypeBuilder& Method(const char* name)
        {
            using Reflect = detail::MethodReflect<Member>;
            const Span<const ParamInfo> params = Reflect::Params();
            m_data.methods.PushBack(MethodInfo{
                name, Reflect::ReturnType(), params.Data(), static_cast<u32>(params.Size()),
                Reflect::isStatic, Reflect::isConst, &Reflect::Invoke });
            return *this;
        }

        template <typename V>
        TypeBuilder& Attribute(const char* key, V value)
        {
            m_data.attributes.PushBack(raptor::core::Attribute{ key, Variant::From<V>(Move(value)) });
            return *this;
        }

        template <typename V>
        TypeBuilder& Constant(const char* name, V value)
        {
            m_data.constants.PushBack(raptor::core::ConstantInfo{
                name, &TypeOf<V>(), Variant::From<V>(Move(value)) });
            return *this;
        }

        [[nodiscard]] TypeData Build()
        {
            m_data.info = MakeTypeInfo<T>(m_name, m_namespace, m_base);
            m_data.info.properties = m_data.properties.Data();
            m_data.info.propertyCount = static_cast<u32>(m_data.properties.Size());
            m_data.info.methods = m_data.methods.Data();
            m_data.info.methodCount = static_cast<u32>(m_data.methods.Size());
            m_data.info.attributes = m_data.attributes.Data();
            m_data.info.attributeCount = static_cast<u32>(m_data.attributes.Size());
            m_data.info.constants = m_data.constants.Data();
            m_data.info.constantCount = static_cast<u32>(m_data.constants.Size());
            return Move(m_data);
        }

    private:
        const char* m_name;
        const char* m_namespace;
        const TypeInfo* m_base;
        TypeData m_data;
    };
}
