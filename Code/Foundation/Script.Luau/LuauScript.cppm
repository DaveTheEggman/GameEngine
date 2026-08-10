// Foundation::Script.Luau - the Luau VM backend for Foundation::Script.
//
// Implements IScriptManager / IScriptContext / ScriptObject / IScriptDelegate over the
// vendored Luau VM + Compiler (ThirdParty/luau, interpreter-only). One isolated
// lua_State per context; reflected engine types are emitted into each state at context
// creation as a metatable (property get/set + method dispatch through the reflection
// currency: Variant in a userdata, ToInstance for the `this`). Coroutines run on a
// host-side scheduler over lua threads (startCoroutine / waitSeconds / waitUntil - the
// same wait model the other backends certify); delegates wrap a registry-ref'd Lua
// function with the Detach-on-close protocol the conformance battery requires.
//
// Error handling: the VM is built with LUA_USE_LONGJMP=1 (the engine is
// -fno-exceptions), and every entry into the VM goes through lua_pcall/lua_resume so a
// script fault lands back here as a status, never unwinding our frames.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include "Core/Reflection/Reflect.h"
#include "Script.Luau/LuauInclude.h"

export module foundation.script.luau;

import foundation.core;
import foundation.script;

namespace core = foundation::core;
using namespace foundation::core;

namespace foundation::script
{
    class LuauScriptManager;
    class LuauScriptContext;

    namespace
    {
        [[nodiscard]] const char* CStr(StringView view, String& storage)
        {
            storage = String(view);
            return reinterpret_cast<const char*>(storage.CStr());
        }

        [[nodiscard]] StringView ViewOf(const char* text)
        {
            return (text != nullptr) ? StringView(reinterpret_cast<const utf8char*>(text))
                                     : StringView{};
        }

        // Is this reflected type one the backend binds? Mirrors the conformance battery's
        // own "bindable" predicate: script-constructible, not an enum, not a container.
        [[nodiscard]] bool IsBindable(const TypeInfo& type)
        {
            return type.constructorCount > 0 && type.enumeratorCount == 0 &&
                   type.container == nullptr && type.name != nullptr;
        }
    }

    // =====================================================================
    // Delegate: a registry-ref'd Lua function. Holding the RefPtr keeps the
    // function GC-alive (lua_ref pins it); Detach() is called by the owning
    // context when the lua_State closes, after which Invoke fails cleanly -
    // the "error if the owning context is gone" contract.
    // =====================================================================
    class LuauScriptDelegate final : public IScriptDelegate
    {
    public:
        LuauScriptDelegate(LuauScriptContext* context, lua_State* state, int functionRef)
            : m_context(context), m_state(state), m_functionRef(functionRef)
        {
        }
        ~LuauScriptDelegate() override;

        void Detach() noexcept
        {
            m_context = nullptr;
            m_state = nullptr;
            m_functionRef = LUA_NOREF;
        }

        [[nodiscard]] Result<Variant> Invoke(Span<Variant> args) override;

    private:
        LuauScriptContext* m_context; // null after Detach
        lua_State* m_state;
        int m_functionRef;
    };

    // =====================================================================
    // ScriptObject: a registry-ref'd Lua table (an instance of a script
    // class). Retains the context.
    // =====================================================================
    class LuauScriptObject final : public ScriptObject
    {
    public:
        LuauScriptObject(RefPtr<LuauScriptContext> context, int tableRef)
            : m_context(Move(context)), m_tableRef(tableRef)
        {
        }
        ~LuauScriptObject() override;

        [[nodiscard]] Result<Variant> Invoke(StringView method, Span<Variant> args) override;

        // Identity of the instance table (coroutine ownership matching).
        [[nodiscard]] const void* TablePointer() const;

    private:
        RefPtr<LuauScriptContext> m_context;
        int m_tableRef;
    };

    // =====================================================================
    // Context: one isolated lua_State.
    // =====================================================================
    class LuauScriptContext final : public IScriptContext
    {
    public:
        explicit LuauScriptContext(RefPtr<LuauScriptManager> manager);
        ~LuauScriptContext() override;

        void SetErrorHandler(IScriptErrorHandler* handler) override { m_errors = handler; }
        Status Load(StringView source, StringView chunkName) override;
        void SetGlobal(StringView name, const Variant& value) override;
        [[nodiscard]] Variant GetGlobal(StringView name) override;
        [[nodiscard]] bool HasFunction(StringView name) const override;
        [[nodiscard]] Result<Variant> Call(StringView function, Span<Variant> args) override;
        [[nodiscard]] RefPtr<ScriptObject> CreateInstance(StringView className,
                                                          Span<Variant> args) override;

        [[nodiscard]] lua_State* State() noexcept { return m_state; }
        [[nodiscard]] LuauScriptManager* Manager() noexcept { return m_manager.Get(); }

        void ReportError(ScriptErrorKind kind, StringView module, StringView message);
        // The message a failed pcall/load left on top of the stack; pops it.
        void ReportTopOfStack(ScriptErrorKind kind, StringView module);

        void TrackDelegate(LuauScriptDelegate* delegate) { m_delegates.PushBack(delegate); }
        void UntrackDelegate(LuauScriptDelegate* delegate)
        {
            for (usize i = 0; i < m_delegates.Size(); ++i)
            {
                if (m_delegates[i] == delegate)
                {
                    m_delegates.RemoveAt(i);
                    return;
                }
            }
        }

        // ---- marshalling (public: the emitter's C closures use these) ----
        void PushVariant(lua_State* state, const Variant& value);
        [[nodiscard]] Variant ToVariant(lua_State* state, int index);
        // Like ToVariant, but honours the reflected parameter/property TYPE the value is bound to:
        // an enum-typed target takes a Lua number carried as i64 (the neutral dispatch casts it to
        // the enum). Use at every native call/setter site where the expected type is known.
        [[nodiscard]] Variant ToVariantForParam(lua_State* state, int index, const TypeInfo* expected);

    private:
        void EmitRegisteredTypes();
        void EmitType(const TypeInfo& type);
        // A reflected enum as a named constant table: _G[EnumName] = { ValueName = <int>, ... }.
        void EmitEnum(const TypeInfo& type);
        void InstallCoroutineApi();

        RefPtr<LuauScriptManager> m_manager;
        lua_State* m_state = nullptr;
        IScriptErrorHandler* m_errors = nullptr;
        Array<LuauScriptDelegate*> m_delegates; // borrowed; Detach()ed at close
    };

    // =====================================================================
    // Manager.
    // =====================================================================
    class LuauScriptManager final : public IScriptManager
    {
    public:
        ~LuauScriptManager() override
        {
            DIAGNOSTIC_ASSERT(m_contexts.IsEmpty()); // contexts retain the manager
        }

        void RegisterType(const TypeInfo& type) override
        {
            for (const TypeInfo* existing : m_types)
            {
                if (existing == &type)
                {
                    return;
                }
            }
            m_types.PushBack(&type);
        }

        [[nodiscard]] RefPtr<IScriptContext> CreateContext() override;

        void CollectGarbage() override
        {
            for (LuauScriptContext* context : m_contexts)
            {
                lua_gc(context->State(), LUA_GCCOLLECT, 0);
            }
        }

        [[nodiscard]] ScriptCapabilities Capabilities() const override
        {
            return ScriptCapabilities::Coroutines | ScriptCapabilities::Delegates;
        }

        [[nodiscard]] Array<ScriptApiType> DescribeBoundApi() const override;

        void AdvanceCoroutines(f64 deltaSeconds) override;
        void CancelCoroutinesFor(ScriptObject& instance) override;

        // ---- backend internals ----
        [[nodiscard]] Span<const TypeInfo* const> RegisteredTypes() const noexcept
        {
            return Span<const TypeInfo* const>{m_types.Data(), m_types.Size()};
        }
        void TrackContext(LuauScriptContext* context) { m_contexts.PushBack(context); }
        void UntrackContext(LuauScriptContext* context)
        {
            DropCoroutinesOf(context);
            for (usize i = 0; i < m_contexts.Size(); ++i)
            {
                if (m_contexts[i] == context)
                {
                    m_contexts.RemoveAt(i);
                    return;
                }
            }
        }

        // One scheduled coroutine: a registry-ref'd lua thread waiting on a timer or a
        // predicate. `owner` is the instance table's pointer identity (CancelCoroutinesFor).
        struct Coroutine
        {
            LuauScriptContext* context = nullptr;
            lua_State* thread = nullptr;
            int threadRef = LUA_NOREF;
            const void* owner = nullptr;
            f64 remainingSeconds = 0.0;
            int predicateRef = LUA_NOREF; // LUA_NOREF = timed wait
            bool started = false;         // first resume pending?
        };
        Array<Coroutine> coroutines;

        // The entry whose thread is `state` (the wait primitives run ON the thread).
        [[nodiscard]] Coroutine* FindCoroutine(lua_State* state)
        {
            for (Coroutine& entry : coroutines)
            {
                if (entry.thread == state)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

    private:
        void DropCoroutinesOf(LuauScriptContext* context);
        void DropCoroutineAt(usize index);
        // Resume one entry; true when it should be REMOVED (finished or faulted).
        [[nodiscard]] bool ResumeCoroutine(Coroutine& entry);

        Array<const TypeInfo*> m_types;
        Array<LuauScriptContext*> m_contexts; // borrowed; contexts retain us
    };

    // =====================================================================
    // Marshalling
    // =====================================================================
    namespace
    {
        // Full userdata payload: an in-place Variant (value or object mode). The
        // userdata destructor (lua_newuserdatadtor) runs ~Variant - releases any
        // RefPtr the object mode holds - with no __gc plumbing.
        void DestroyVariantUserdata(void* payload) { static_cast<Variant*>(payload)->~Variant(); }

        [[nodiscard]] Variant* VariantAt(lua_State* state, int index)
        {
            if (lua_type(state, index) != LUA_TUSERDATA)
            {
                return nullptr;
            }
            return static_cast<Variant*>(lua_touserdata(state, index));
        }
    }

    void LuauScriptContext::PushVariant(lua_State* state, const Variant& value)
    {
        if (value.IsEmpty())
        {
            lua_pushnil(state);
            return;
        }
        if (const bool* b = value.TryGet<bool>())
        {
            lua_pushboolean(state, *b ? 1 : 0);
            return;
        }
        if (const f64* d = value.TryGet<f64>())
        {
            lua_pushnumber(state, *d);
            return;
        }
        if (const f32* f = value.TryGet<f32>())
        {
            lua_pushnumber(state, static_cast<f64>(*f));
            return;
        }
        if (const i32* i = value.TryGet<i32>())
        {
            lua_pushnumber(state, static_cast<f64>(*i));
            return;
        }
        if (const i64* i = value.TryGet<i64>())
        {
            lua_pushnumber(state, static_cast<f64>(*i));
            return;
        }
        if (const u32* u = value.TryGet<u32>())
        {
            lua_pushnumber(state, static_cast<f64>(*u));
            return;
        }
        if (const String* s = value.TryGet<String>())
        {
            lua_pushlstring(state, reinterpret_cast<const char*>(s->CStr()), s->Size());
            return;
        }
        // An enum crosses as its underlying number: Lua has no enum type, the emitter excludes
        // enums from binding, and an enum parameter converts the number back (mirrors Wren +
        // AngelScript). Must come before the boxed-Variant fallthrough (an enum Variant is not a
        // primitive TryGet match).
        if (const TypeInfo* enumType = value.Type();
            enumType != nullptr && enumType->enumeratorCount > 0)
        {
            lua_pushnumber(state, static_cast<f64>(value.AsEnumInt()));
            return;
        }
        // Everything else (objects, reflected values) travels as a boxed Variant with the
        // dynamic type's dispatch metatable, so script sees properties/methods directly.
        void* payload = lua_newuserdatadtor(state, sizeof(Variant), DestroyVariantUserdata);
        new (payload) Variant(value);
        const TypeInfo* type = value.Type();
        if (type != nullptr)
        {
            // The per-type metatable was created at emission when the type is bound; a
            // NON-bound type still gets a plain metatable-free userdata (opaque handle).
            lua_pushlightuserdata(state, const_cast<TypeInfo*>(type));
            lua_rawget(state, LUA_REGISTRYINDEX);
            if (lua_istable(state, -1))
            {
                lua_setmetatable(state, -2);
            }
            else
            {
                lua_pop(state, 1);
            }
        }
    }

    Variant LuauScriptContext::ToVariant(lua_State* state, int index)
    {
        switch (lua_type(state, index))
        {
        case LUA_TNIL:
            return Variant{};
        case LUA_TBOOLEAN:
            return Variant::From<bool>(lua_toboolean(state, index) != 0);
        case LUA_TNUMBER:
            return Variant::From<f64>(lua_tonumber(state, index));
        case LUA_TSTRING:
        {
            size_t length = 0;
            const char* text = lua_tolstring(state, index, &length);
            String value;
            value.Append(reinterpret_cast<const utf8char*>(text), length);
            return Variant::From<String>(Move(value));
        }
        case LUA_TUSERDATA:
        {
            if (const Variant* boxed = VariantAt(state, index))
            {
                return *boxed;
            }
            return Variant{};
        }
        case LUA_TFUNCTION:
        {
            // A script function crossing into native = a delegate (the reflected param
            // side matches it by the IScriptDelegate object type).
            lua_pushvalue(state, index);
            const int ref = lua_ref(state, -1);
            lua_pop(state, 1);
            RefPtr<LuauScriptDelegate> delegate = MakeRef<LuauScriptDelegate>(
                DefaultAllocator(), this, m_state, ref);
            TrackDelegate(delegate.Get());
            return Variant::From<RefPtr<Object>>(RefPtr<Object>(delegate.Get()));
        }
        default:
            return Variant{};
        }
    }

    Variant LuauScriptContext::ToVariantForParam(lua_State* state, int index,
                                                 const TypeInfo* expected)
    {
        // An enum parameter/setter takes a Lua number; carry it as an i64 - the neutral property
        // setter / enum-arg path casts it to the enum (enums cross as their underlying int, with
        // no Lua enum type to marshal through). Anything else uses the ordinary conversion.
        if (expected != nullptr && expected->enumeratorCount > 0 &&
            lua_type(state, index) == LUA_TNUMBER)
        {
            return Variant::From<i64>(static_cast<i64>(lua_tonumber(state, index)));
        }
        return ToVariant(state, index);
    }

    // =====================================================================
    // Reflected-type emission: one metatable per TypeInfo per state.
    //
    // Metatable layout:
    //   registry[TypeInfo*] = mt { __index = C(__index), __newindex = C(__newindex) }
    //   mt's C closures carry: upvalue1 = TypeInfo* (light), upvalue2 = context* (light),
    //   upvalue3 = methods table { name -> C(method) with upvalues MethodInfo*/context* }.
    // Class access: _G[Name] = class table { new = C(ctor), <statics> } so scripts write
    // Name.new(...) and instance:Method(...).
    // =====================================================================
    namespace
    {
        struct DispatchUpvalues
        {
            static constexpr int kType = 1;
            static constexpr int kContext = 2;
            static constexpr int kMethods = 3;
        };

        [[nodiscard]] const PropertyInfo* FindPropertyInChain(const TypeInfo* type,
                                                              const char* name)
        {
            for (const TypeInfo* t = type; t != nullptr; t = t->base)
            {
                if (const PropertyInfo* property = FindProperty(*t, name))
                {
                    return property;
                }
            }
            return nullptr;
        }

        int MethodThunk(lua_State* state)
        {
            auto* method = static_cast<const MethodInfo*>(
                lua_tolightuserdata(state, lua_upvalueindex(1)));
            auto* context = static_cast<LuauScriptContext*>(
                lua_tolightuserdata(state, lua_upvalueindex(2)));

            const int argCount = lua_gettop(state);
            Variant self;
            Array<Variant> args;
            int firstArg = 1;
            if (!method->isStatic)
            {
                Variant* boxed = VariantAt(state, 1);
                if (boxed == nullptr)
                {
                    lua_pushstring(state, "method called without a bound self (use ':')");
                    lua_error(state);
                }
                self = *boxed;
                firstArg = 2;
            }
            for (int i = firstArg; i <= argCount; ++i)
            {
                // Honour the reflected param type for this arg (enum -> number carried as i64);
                // extra args beyond the signature fall back to plain conversion.
                const u32 paramIndex = static_cast<u32>(i - firstArg);
                const TypeInfo* expected =
                    (paramIndex < method->paramCount && method->params[paramIndex].type != nullptr)
                        ? method->params[paramIndex].type()
                        : nullptr;
                args.PushBack(context->ToVariantForParam(state, i, expected));
            }

            ScriptCallScope scope(context);
            Result<Variant> result =
                method->isStatic
                    ? InvokeStatic(*method, Span<Variant>{args.Data(), args.Size()})
                    : InvokeMethod(*method, ToInstance(self),
                                   Span<Variant>{args.Data(), args.Size()});
            if (!result.HasValue())
            {
                lua_pushstring(state, "reflected method invocation failed");
                lua_error(state);
            }
            if (result.Value().IsEmpty())
            {
                return 0;
            }
            context->PushVariant(state, result.Value());
            return 1;
        }

        int IndexThunk(lua_State* state)
        {
            // (userdata, key) -> method (from the methods table) or property value.
            auto* type = static_cast<const TypeInfo*>(
                lua_tolightuserdata(state, lua_upvalueindex(DispatchUpvalues::kType)));
            auto* context = static_cast<LuauScriptContext*>(
                lua_tolightuserdata(state, lua_upvalueindex(DispatchUpvalues::kContext)));

            // Methods first: the common case, one rawget.
            lua_pushvalue(state, 2);
            lua_rawget(state, lua_upvalueindex(DispatchUpvalues::kMethods));
            if (!lua_isnil(state, -1))
            {
                return 1;
            }
            lua_pop(state, 1);

            const char* key = lua_tostring(state, 2);
            Variant* boxed = VariantAt(state, 1);
            if (key == nullptr || boxed == nullptr)
            {
                lua_pushnil(state);
                return 1;
            }
            if (const PropertyInfo* property = FindPropertyInChain(type, key))
            {
                ScriptCallScope scope(context);
                Instance instance = ToInstance(*boxed);
                if (instance.Pointer() == nullptr)
                {
                    lua_pushnil(state);
                    return 1;
                }
                context->PushVariant(state, GetProperty(*property, instance));
                return 1;
            }
            lua_pushnil(state);
            return 1;
        }

        int NewIndexThunk(lua_State* state)
        {
            auto* type = static_cast<const TypeInfo*>(
                lua_tolightuserdata(state, lua_upvalueindex(DispatchUpvalues::kType)));
            auto* context = static_cast<LuauScriptContext*>(
                lua_tolightuserdata(state, lua_upvalueindex(DispatchUpvalues::kContext)));

            const char* key = lua_tostring(state, 2);
            Variant* boxed = VariantAt(state, 1);
            const PropertyInfo* property =
                (key != nullptr) ? FindPropertyInChain(type, key) : nullptr;
            if (boxed == nullptr || property == nullptr)
            {
                lua_pushstring(state, "no such reflected property");
                lua_error(state);
            }
            ScriptCallScope scope(context);
            Instance instance = ToInstance(*boxed);
            if (instance.Pointer() != nullptr)
            {
                // Honour the property type (enum -> number carried as i64).
                Variant value = context->ToVariantForParam(state, 3, property->type);
                (void)SetProperty(*property, instance, value);
            }
            return 0;
        }

        int ConstructorThunk(lua_State* state)
        {
            auto* type = static_cast<const TypeInfo*>(lua_tolightuserdata(state, lua_upvalueindex(1)));
            auto* context = static_cast<LuauScriptContext*>(
                lua_tolightuserdata(state, lua_upvalueindex(2)));

            const int argCount = lua_gettop(state);
            Array<Variant> args;
            for (int i = 1; i <= argCount; ++i)
            {
                args.PushBack(context->ToVariant(state, i));
            }
            ScriptCallScope scope(context);
            Result<Variant> constructed = Construct(*type, Span<Variant>{args.Data(), args.Size()});
            if (!constructed.HasValue())
            {
                lua_pushstring(state, "no matching reflected constructor");
                lua_error(state);
            }
            context->PushVariant(state, constructed.Value());
            return 1;
        }
    }

    void LuauScriptContext::EmitType(const TypeInfo& type)
    {
        lua_State* state = m_state;

        // The methods table: every instance/static method in the chain as a thunk.
        lua_newtable(state);
        for (const TypeInfo* t = &type; t != nullptr; t = t->base)
        {
            for (const MethodInfo& method : Methods(*t))
            {
                if (method.name == nullptr)
                {
                    continue;
                }
                lua_pushstring(state, method.name);
                // Skip if a derived type already bound this name (first wins).
                lua_pushvalue(state, -1);
                lua_rawget(state, -3);
                const bool taken = !lua_isnil(state, -1);
                lua_pop(state, 1);
                if (taken)
                {
                    lua_pop(state, 1);
                    continue;
                }
                lua_pushlightuserdata(state, const_cast<MethodInfo*>(&method));
                lua_pushlightuserdata(state, this);
                lua_pushcclosure(state, MethodThunk, "reflected_method", 2);
                lua_rawset(state, -3);
            }
        }
        const int methodsIndex = lua_gettop(state);

        // The dispatch metatable, keyed in the registry by TypeInfo pointer.
        lua_newtable(state);
        lua_pushstring(state, "__index");
        lua_pushlightuserdata(state, const_cast<TypeInfo*>(&type));
        lua_pushlightuserdata(state, this);
        lua_pushvalue(state, methodsIndex);
        lua_pushcclosure(state, IndexThunk, "reflected_index", 3);
        lua_rawset(state, -3);
        lua_pushstring(state, "__newindex");
        lua_pushlightuserdata(state, const_cast<TypeInfo*>(&type));
        lua_pushlightuserdata(state, this);
        lua_pushvalue(state, methodsIndex);
        lua_pushcclosure(state, NewIndexThunk, "reflected_newindex", 3);
        lua_rawset(state, -3);

        lua_pushlightuserdata(state, const_cast<TypeInfo*>(&type));
        lua_pushvalue(state, -2);
        lua_rawset(state, LUA_REGISTRYINDEX);
        lua_pop(state, 1); // metatable

        // The class table: Name.new(...) plus the static methods.
        lua_newtable(state);
        lua_pushstring(state, "new");
        lua_pushlightuserdata(state, const_cast<TypeInfo*>(&type));
        lua_pushlightuserdata(state, this);
        lua_pushcclosure(state, ConstructorThunk, "reflected_new", 2);
        lua_rawset(state, -3);
        for (const TypeInfo* t = &type; t != nullptr; t = t->base)
        {
            for (const MethodInfo& method : Methods(*t))
            {
                if (method.name == nullptr || !method.isStatic)
                {
                    continue;
                }
                lua_pushstring(state, method.name);
                lua_pushlightuserdata(state, const_cast<MethodInfo*>(&method));
                lua_pushlightuserdata(state, this);
                lua_pushcclosure(state, MethodThunk, "reflected_static", 2);
                lua_rawset(state, -3);
            }
        }
        lua_setglobal(state, type.name);

        lua_pop(state, 1); // methods table
    }

    void LuauScriptContext::EmitEnum(const TypeInfo& type)
    {
        lua_State* state = m_state;
        // Scripts spell an enum value as EnumName.ValueName (matching AngelScript's native enums;
        // Wren has none). Values are numbers - an enum parameter converts the number back
        // (ToVariantForParam), so the round-trip is exact.
        lua_newtable(state);
        for (const EnumValue& value : Enumerators(type))
        {
            lua_pushstring(state, value.name);
            lua_pushnumber(state, static_cast<f64>(value.value));
            lua_rawset(state, -3);
        }
        lua_setglobal(state, type.name);
    }

    void LuauScriptContext::EmitRegisteredTypes()
    {
        for (const TypeInfo* type : m_manager->RegisteredTypes())
        {
            if (type == nullptr)
            {
                continue;
            }
            // Enums emit as named constant tables; everything bindable emits its class + metatable.
            if (type->enumeratorCount > 0)
            {
                EmitEnum(*type);
            }
            else if (IsBindable(*type))
            {
                EmitType(*type);
            }
        }
    }

    // =====================================================================
    // Coroutines: startCoroutine(fn[, owner]) / waitSeconds(t) / waitUntil(fn).
    // The scheduler lives on the manager; the primitives run ON the coroutine
    // thread and yield after recording the wait on its entry.
    // =====================================================================
    namespace
    {
        int StartCoroutineThunk(lua_State* state)
        {
            auto* context =
                static_cast<LuauScriptContext*>(lua_tolightuserdata(state, lua_upvalueindex(1)));
            if (!lua_isfunction(state, 1))
            {
                lua_pushstring(state, "startCoroutine expects a function");
                lua_error(state);
            }
            LuauScriptManager* manager = context->Manager();

            lua_State* thread = lua_newthread(state); // pushed on the stack
            const int threadRef = lua_ref(state, -1);
            lua_pop(state, 1);
            lua_pushvalue(state, 1);
            lua_xmove(state, thread, 1); // the body function, now on the thread

            LuauScriptManager::Coroutine entry;
            entry.context = context;
            entry.thread = thread;
            entry.threadRef = threadRef;
            entry.owner = (lua_gettop(state) >= 2 && lua_istable(state, 2))
                              ? lua_topointer(state, 2)
                              : nullptr;
            manager->coroutines.PushBack(entry);
            // First resume happens in AdvanceCoroutines (started=false) so a coroutine
            // begun mid-script-call never re-enters the VM re-entrantly here.
            return 0;
        }

        int WaitSecondsThunk(lua_State* state)
        {
            auto* context =
                static_cast<LuauScriptContext*>(lua_tolightuserdata(state, lua_upvalueindex(1)));
            LuauScriptManager::Coroutine* entry = context->Manager()->FindCoroutine(state);
            if (entry == nullptr)
            {
                lua_pushstring(state, "waitSeconds outside a startCoroutine coroutine");
                lua_error(state);
            }
            entry->remainingSeconds = lua_tonumber(state, 1);
            entry->predicateRef = LUA_NOREF;
            return lua_yield(state, 0);
        }

        int WaitUntilThunk(lua_State* state)
        {
            auto* context =
                static_cast<LuauScriptContext*>(lua_tolightuserdata(state, lua_upvalueindex(1)));
            LuauScriptManager::Coroutine* entry = context->Manager()->FindCoroutine(state);
            if (entry == nullptr || !lua_isfunction(state, 1))
            {
                lua_pushstring(state, "waitUntil expects a predicate, inside a coroutine");
                lua_error(state);
            }
            lua_pushvalue(state, 1);
            entry->predicateRef = lua_ref(state, -1);
            lua_pop(state, 1);
            entry->remainingSeconds = 0.0;
            return lua_yield(state, 0);
        }
    }

    void LuauScriptContext::InstallCoroutineApi()
    {
        lua_pushlightuserdata(m_state, this);
        lua_pushcclosure(m_state, StartCoroutineThunk, "startCoroutine", 1);
        lua_setglobal(m_state, "startCoroutine");
        lua_pushlightuserdata(m_state, this);
        lua_pushcclosure(m_state, WaitSecondsThunk, "waitSeconds", 1);
        lua_setglobal(m_state, "waitSeconds");
        lua_pushlightuserdata(m_state, this);
        lua_pushcclosure(m_state, WaitUntilThunk, "waitUntil", 1);
        lua_setglobal(m_state, "waitUntil");
    }

    bool LuauScriptManager::ResumeCoroutine(Coroutine& entry)
    {
        ScriptCallScope scope(entry.context);
        const int status = lua_resume(entry.thread, nullptr, 0);
        if (status == LUA_YIELD)
        {
            return false; // recorded a new wait; stays scheduled
        }
        if (status != LUA_OK)
        {
            const char* message = lua_tostring(entry.thread, -1);
            entry.context->ReportError(ScriptErrorKind::Runtime, u8"coroutine", ViewOf(message));
        }
        return true; // finished or faulted -> drop
    }

    void LuauScriptManager::AdvanceCoroutines(f64 deltaSeconds)
    {
        for (usize i = 0; i < coroutines.Size();)
        {
            Coroutine& entry = coroutines[i];
            bool drop = false;
            if (!entry.started)
            {
                entry.started = true;
                drop = ResumeCoroutine(entry);
            }
            else if (entry.predicateRef != LUA_NOREF)
            {
                lua_State* state = entry.context->State();
                lua_getref(state, entry.predicateRef);
                bool ready = false;
                if (lua_pcall(state, 0, 1, 0) == LUA_OK)
                {
                    ready = lua_toboolean(state, -1) != 0;
                    lua_pop(state, 1);
                }
                else
                {
                    lua_pop(state, 1);
                    drop = true; // faulting predicate cancels the coroutine
                }
                if (!drop && ready)
                {
                    lua_unref(state, entry.predicateRef);
                    entry.predicateRef = LUA_NOREF;
                    drop = ResumeCoroutine(entry);
                }
            }
            else
            {
                entry.remainingSeconds -= deltaSeconds;
                if (entry.remainingSeconds <= 0.0)
                {
                    drop = ResumeCoroutine(entry);
                }
            }

            if (drop)
            {
                DropCoroutineAt(i);
            }
            else
            {
                ++i;
            }
        }
    }

    void LuauScriptManager::CancelCoroutinesFor(ScriptObject& instance)
    {
        const void* owner = static_cast<LuauScriptObject&>(instance).TablePointer();
        if (owner == nullptr)
        {
            return;
        }
        for (usize i = 0; i < coroutines.Size();)
        {
            if (coroutines[i].owner == owner)
            {
                DropCoroutineAt(i);
            }
            else
            {
                ++i;
            }
        }
    }

    void LuauScriptManager::DropCoroutineAt(usize index)
    {
        Coroutine& entry = coroutines[index];
        lua_State* state = entry.context->State();
        if (entry.predicateRef != LUA_NOREF)
        {
            lua_unref(state, entry.predicateRef);
        }
        if (entry.threadRef != LUA_NOREF)
        {
            lua_unref(state, entry.threadRef);
        }
        coroutines.RemoveAt(index);
    }

    void LuauScriptManager::DropCoroutinesOf(LuauScriptContext* context)
    {
        for (usize i = 0; i < coroutines.Size();)
        {
            if (coroutines[i].context == context)
            {
                DropCoroutineAt(i);
            }
            else
            {
                ++i;
            }
        }
    }

    // =====================================================================
    // Context lifecycle + the IScriptContext surface.
    // =====================================================================
    LuauScriptContext::LuauScriptContext(RefPtr<LuauScriptManager> manager)
        : m_manager(Move(manager))
    {
        m_state = luaL_newstate();
        luaL_openlibs(m_state); // Luau's SANDBOXED stdlib (no io/os/loadstring)
        InstallCoroutineApi();
        EmitRegisteredTypes();
        m_manager->TrackContext(this);
    }

    LuauScriptContext::~LuauScriptContext()
    {
        m_manager->UntrackContext(this);
        // The Detach protocol: delegates outliving the context must fail cleanly, never
        // touch a dead lua_State (the conformance battery's owning-context-gone contract).
        for (LuauScriptDelegate* delegate : m_delegates)
        {
            delegate->Detach();
        }
        m_delegates.Clear();
        lua_close(m_state);
    }

    void LuauScriptContext::ReportError(ScriptErrorKind kind, StringView module,
                                        StringView message)
    {
        if (m_errors == nullptr)
        {
            String line(message);
            line += u8"\n";
            ConsoleWriteError(line.AsView());
            return;
        }
        ScriptError error;
        error.kind = kind;
        error.module = module;
        error.line = -1;
        error.message = message;
        m_errors->OnError(error);
    }

    void LuauScriptContext::ReportTopOfStack(ScriptErrorKind kind, StringView module)
    {
        const char* message = lua_tostring(m_state, -1);
        ReportError(kind, module, ViewOf(message));
        lua_pop(m_state, 1);
    }

    Status LuauScriptContext::Load(StringView source, StringView chunkName)
    {
        String chunkStorage;
        const char* chunk = CStr(chunkName, chunkStorage);

        size_t bytecodeSize = 0;
        char* bytecode = luau_compile(reinterpret_cast<const char*>(source.Data()),
                                      source.Size(), nullptr, &bytecodeSize);
        const int loadStatus = luau_load(m_state, chunk, bytecode, bytecodeSize, 0);
        free(bytecode);
        if (loadStatus != LUA_OK)
        {
            // Luau encodes compile errors in the bytecode; they surface here.
            ReportTopOfStack(ScriptErrorKind::Compile, chunkName);
            return Status{ErrorCode::InvalidArgument};
        }

        ScriptCallScope scope(this);
        if (lua_pcall(m_state, 0, 0, 0) != LUA_OK)
        {
            ReportTopOfStack(ScriptErrorKind::Runtime, chunkName);
            return Status{ErrorCode::Unknown};
        }
        return Status{};
    }

    void LuauScriptContext::SetGlobal(StringView name, const Variant& value)
    {
        String storage;
        PushVariant(m_state, value);
        lua_setglobal(m_state, CStr(name, storage));
    }

    Variant LuauScriptContext::GetGlobal(StringView name)
    {
        String storage;
        lua_getglobal(m_state, CStr(name, storage));
        Variant value = ToVariant(m_state, -1);
        lua_pop(m_state, 1);
        return value;
    }

    bool LuauScriptContext::HasFunction(StringView name) const
    {
        String storage;
        auto* self = const_cast<LuauScriptContext*>(this);
        lua_getglobal(self->m_state, CStr(name, storage));
        const bool isFunction = lua_isfunction(self->m_state, -1);
        lua_pop(self->m_state, 1);
        return isFunction;
    }

    Result<Variant> LuauScriptContext::Call(StringView function, Span<Variant> args)
    {
        String storage;
        lua_getglobal(m_state, CStr(function, storage));
        if (!lua_isfunction(m_state, -1))
        {
            lua_pop(m_state, 1);
            return Err(ErrorCode::NotFound);
        }
        for (const Variant& arg : args)
        {
            PushVariant(m_state, arg);
        }
        ScriptCallScope scope(this);
        if (lua_pcall(m_state, static_cast<int>(args.Size()), 1, 0) != LUA_OK)
        {
            ReportTopOfStack(ScriptErrorKind::Runtime, function);
            return Err(ErrorCode::Unknown);
        }
        Variant result = ToVariant(m_state, -1);
        lua_pop(m_state, 1);
        return result;
    }

    RefPtr<ScriptObject> LuauScriptContext::CreateInstance(StringView className,
                                                           Span<Variant> args)
    {
        String storage;
        lua_getglobal(m_state, CStr(className, storage));
        if (!lua_istable(m_state, -1))
        {
            lua_pop(m_state, 1);
            return {};
        }
        lua_getfield(m_state, -1, "new");
        lua_remove(m_state, -2); // the class table
        if (!lua_isfunction(m_state, -1))
        {
            lua_pop(m_state, 1);
            return {};
        }
        for (const Variant& arg : args)
        {
            PushVariant(m_state, arg);
        }
        ScriptCallScope scope(this);
        if (lua_pcall(m_state, static_cast<int>(args.Size()), 1, 0) != LUA_OK)
        {
            ReportTopOfStack(ScriptErrorKind::Runtime, className);
            return {};
        }
        if (!lua_istable(m_state, -1))
        {
            lua_pop(m_state, 1);
            return {};
        }
        const int tableRef = lua_ref(m_state, -1);
        lua_pop(m_state, 1);
        return RefPtr<ScriptObject>(
            MakeRef<LuauScriptObject>(DefaultAllocator(), RefPtr<LuauScriptContext>(this), tableRef)
                .Get());
    }

    // =====================================================================
    // ScriptObject + delegate bodies.
    // =====================================================================
    LuauScriptObject::~LuauScriptObject()
    {
        lua_unref(m_context->State(), m_tableRef);
    }

    const void* LuauScriptObject::TablePointer() const
    {
        lua_State* state = m_context->State();
        lua_getref(state, m_tableRef);
        const void* pointer = lua_topointer(state, -1);
        lua_pop(state, 1);
        return pointer;
    }

    Result<Variant> LuauScriptObject::Invoke(StringView method, Span<Variant> args)
    {
        lua_State* state = m_context->State();
        String storage;
        lua_getref(state, m_tableRef);
        lua_getfield(state, -1, CStr(method, storage)); // honors the class metatable chain
        if (!lua_isfunction(state, -1))
        {
            lua_pop(state, 2);
            return Err(ErrorCode::NotFound);
        }
        lua_pushvalue(state, -2); // self
        lua_remove(state, -3);
        for (const Variant& arg : args)
        {
            m_context->PushVariant(state, arg);
        }
        ScriptCallScope scope(m_context.Get());
        if (lua_pcall(state, static_cast<int>(args.Size()) + 1, 1, 0) != LUA_OK)
        {
            m_context->ReportTopOfStack(ScriptErrorKind::Runtime, method);
            return Err(ErrorCode::Unknown);
        }
        Variant result = m_context->ToVariant(state, -1);
        lua_pop(state, 1);
        return result;
    }

    LuauScriptDelegate::~LuauScriptDelegate()
    {
        if (m_context != nullptr)
        {
            m_context->UntrackDelegate(this);
            lua_unref(m_state, m_functionRef);
        }
    }

    Result<Variant> LuauScriptDelegate::Invoke(Span<Variant> args)
    {
        if (m_context == nullptr || m_functionRef == LUA_NOREF)
        {
            return Err(ErrorCode::Internal); // the owning context is gone
        }
        lua_State* state = m_state;
        lua_getref(state, m_functionRef);
        for (const Variant& arg : args)
        {
            m_context->PushVariant(state, arg);
        }
        ScriptCallScope scope(m_context);
        if (lua_pcall(state, static_cast<int>(args.Size()), 1, 0) != LUA_OK)
        {
            m_context->ReportTopOfStack(ScriptErrorKind::Runtime, u8"delegate");
            return Err(ErrorCode::Unknown);
        }
        Variant result = m_context->ToVariant(state, -1);
        lua_pop(state, 1);
        return result;
    }

    // =====================================================================
    // Manager: contexts + the bound-API report.
    // =====================================================================
    RefPtr<IScriptContext> LuauScriptManager::CreateContext()
    {
        return RefPtr<IScriptContext>(
            MakeRef<LuauScriptContext>(DefaultAllocator(), RefPtr<LuauScriptManager>(this)).Get());
    }

    Array<ScriptApiType> LuauScriptManager::DescribeBoundApi() const
    {
        Array<ScriptApiType> surface;
        for (const TypeInfo* type : m_types)
        {
            if (type == nullptr || !IsBindable(*type))
            {
                continue;
            }
            ScriptApiType api;
            api.scriptName = String(ViewOf(type->name));
            api.typeId = type->id;

            ScriptApiMember ctor;
            ctor.name = String(u8"new");
            ctor.kind = ScriptApiMemberKind::Method;
            ctor.isStatic = true;
            ctor.signature = api.scriptName;
            ctor.signature += u8".new(...)";
            api.members.PushBack(Move(ctor));

            for (const TypeInfo* t = type; t != nullptr; t = t->base)
            {
                for (const MethodInfo& method : Methods(*t))
                {
                    if (method.name == nullptr)
                    {
                        continue;
                    }
                    ScriptApiMember member;
                    member.name = String(ViewOf(method.name));
                    member.kind = ScriptApiMemberKind::Method;
                    member.isStatic = method.isStatic;
                    member.signature = member.name;
                    member.signature += u8"(";
                    for (u32 p = 0; p < method.paramCount; ++p)
                    {
                        if (p > 0)
                        {
                            member.signature += u8", ";
                        }
                        const ParamInfo& param = method.params[p];
                        member.signature +=
                            (param.name != nullptr && param.name[0] != '\0')
                                ? ViewOf(param.name)
                                : StringView(u8"arg");
                    }
                    member.signature += u8")";
                    api.members.PushBack(Move(member));
                }
                for (u32 p = 0; p < t->propertyCount; ++p)
                {
                    if (t->properties[p].name == nullptr)
                    {
                        continue;
                    }
                    ScriptApiMember member;
                    member.name = String(ViewOf(t->properties[p].name));
                    member.kind = ScriptApiMemberKind::Property;
                    member.signature = member.name;
                    api.members.PushBack(Move(member));
                }
            }
            surface.PushBack(Move(api));
        }
        return surface;
    }
}

export namespace foundation::script
{
    /// The backend factory: a Luau IScriptManager (interpreter-only, longjmp error
    /// unwinding, sandboxed stdlib). Language id: "luau".
    [[nodiscard]] core::RefPtr<IScriptManager> CreateLuauScriptManager()
    {
        return core::RefPtr<IScriptManager>(
            core::MakeRef<LuauScriptManager>(core::DefaultAllocator()).Get());
    }

    /// Registers Luau with the backend registry (scripting.md B1) - the ONE line that makes the
    /// language available; consumers resolve by extension/language, never by type.
    inline void RegisterLuauScriptBackend()
    {
        ScriptBackendDesc desc;
        desc.languageId = core::String(u8"luau");
        desc.displayName = core::String(u8"Luau");
        desc.fileExtensions.PushBack(core::String(u8"luau"));
        desc.create = []() { return CreateLuauScriptManager(); };
        ScriptBackendRegistry::Get().Register(core::Move(desc));
    }
}
