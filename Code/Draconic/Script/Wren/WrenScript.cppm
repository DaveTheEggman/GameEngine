// Draconic::ScriptWren - Wren VM backend (draconic.script.wren).
//
// Implements Draconic::Script on Wren and binds reflected types into the VM:
// each registered type with a constructor becomes a Wren `foreign class` whose
// allocate/getters/setters route through reflection (Construct / GetProperty /
// SetProperty), with values held in a Variant as the foreign instance data.
//
// Wren foreign callbacks are plain C function pointers (no captures), so method
// dispatch uses a fixed pool of trampolines: each Trampoline<I> forwards to
// g_bindings[I], assigned when Wren asks us to bind a (class, signature).
//
// This slice: construction + property get/set for value types. Reflected
// methods and object/struct returns (wrapping a Variant back into a foreign
// instance) come next.

module;
#include "Core/Prelude.h"
#include "WrenInclude.h"
#include <utility>

export module draconic.script.wren;

import draconic.core;
import draconic.script;

namespace core = draconic::core;

namespace draconic::script::wren
{
    inline const char* CStr(const core::String& s) noexcept
    {
        return reinterpret_cast<const char*>(s.CStr());
    }

    inline void AppendAscii(core::String& s, const char* text)
    {
        s.Append(core::StringView(reinterpret_cast<const core::utf8char*>(text)));
    }

    inline void AppendUint(core::String& s, core::u32 n)
    {
        char buf[16];
        int i = 0;
        if (n == 0) { buf[i++] = '0'; }
        else
        {
            char tmp[16];
            int j = 0;
            while (n != 0) { tmp[j++] = static_cast<char>('0' + (n % 10)); n /= 10; }
            while (j != 0) { buf[i++] = tmp[--j]; }
        }
        buf[i] = '\0';
        AppendAscii(s, buf);
    }

    inline bool NameEq(const char* a, const char* b) noexcept
    {
        core::usize i = 0;
        while (a[i] != '\0' && a[i] == b[i]) { ++i; }
        return a[i] == b[i];
    }

    inline void WriteUtf8(void (*sink)(core::StringView) noexcept, const char* text)
    {
        if (text != nullptr)
        {
            sink(core::StringView(reinterpret_cast<const core::utf8char*>(text)));
        }
    }

    // --- marshalling -------------------------------------------------------
    inline constexpr const char* kModule = "main"; // module the foreign classes live in

    // Engine value -> Wren slot for primitives; true if handled (slot untouched
    // and false otherwise, so the caller can try a foreign wrap).
    inline bool TryPrimitiveOut(WrenVM* vm, int slot, const core::Variant& value)
    {
        if (const bool* b = value.TryGet<bool>())       { wrenSetSlotBool(vm, slot, *b); return true; }
        if (const core::f64* d = value.TryGet<core::f64>()) { wrenSetSlotDouble(vm, slot, *d); return true; }
        if (const core::f32* f = value.TryGet<core::f32>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*f)); return true; }
        if (const core::i32* i = value.TryGet<core::i32>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*i)); return true; }
        if (const core::i64* i = value.TryGet<core::i64>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*i)); return true; }
        if (const core::u32* u = value.TryGet<core::u32>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*u)); return true; }
        if (const core::u64* u = value.TryGet<core::u64>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*u)); return true; }
        if (const core::String* s = value.TryGet<core::String>())
        {
            // Engine String is UTF-8, as are Wren strings - pass bytes directly.
            wrenSetSlotBytes(vm, slot, CStr(*s), s->Size());
            return true;
        }
        return false;
    }

    // Engine value -> Wren slot. Primitives go directly; a reflected value/object
    // is wrapped into a new Wren foreign instance of its class (if one is bound),
    // sharing/copying the Variant; otherwise null.
    inline void MarshalOut(WrenVM* vm, int slot, const core::Variant& value)
    {
        if (TryPrimitiveOut(vm, slot, value)) { return; }

        const core::TypeInfo* type = value.Type();
        if (type != nullptr && type->name != nullptr &&
            wrenHasModule(vm, kModule) && wrenHasVariable(vm, kModule, type->name))
        {
            const int classSlot = wrenGetSlotCount(vm);
            wrenEnsureSlots(vm, classSlot + 1);
            wrenGetVariable(vm, kModule, type->name, classSlot);
            void* data = wrenSetSlotNewForeign(vm, slot, classSlot, sizeof(core::Variant*));
            *static_cast<core::Variant**>(data) = core::DefaultAllocator().New<core::Variant>(value);
            return;
        }
        wrenSetSlotNull(vm, slot);
    }

    inline core::Variant SlotToVariant(WrenVM* vm, int slot)
    {
        switch (wrenGetSlotType(vm, slot))
        {
            case WREN_TYPE_BOOL: return core::Variant::From<bool>(wrenGetSlotBool(vm, slot));
            case WREN_TYPE_NUM:  return core::Variant::From<core::f64>(wrenGetSlotDouble(vm, slot));
            case WREN_TYPE_FOREIGN: return **static_cast<core::Variant**>(wrenGetSlotForeign(vm, slot));
            case WREN_TYPE_STRING:
            {
                int length = 0;
                const char* bytes = wrenGetSlotBytes(vm, slot, &length);
                return core::Variant::From<core::String>(
                    core::String(core::StringView(reinterpret_cast<const core::utf8char*>(bytes), static_cast<core::usize>(length))));
            }
            default: return core::Variant{};
        }
    }

    // Wren slot -> engine Variant of the expected reflected type (coerces Wren
    // numbers to the target scalar; foreign slots carry a Variant already).
    inline core::Variant MarshalIn(WrenVM* vm, int slot, const core::TypeInfo* expected)
    {
        switch (wrenGetSlotType(vm, slot))
        {
            case WREN_TYPE_FOREIGN:
                return **static_cast<core::Variant**>(wrenGetSlotForeign(vm, slot)); // boxed Variant*
            case WREN_TYPE_BOOL:
                return core::Variant::From<bool>(wrenGetSlotBool(vm, slot));
            case WREN_TYPE_NUM:
            {
                const double d = wrenGetSlotDouble(vm, slot);
                if (expected == &core::TypeOf<core::f32>()) { return core::Variant::From<core::f32>(static_cast<core::f32>(d)); }
                if (expected == &core::TypeOf<core::i32>()) { return core::Variant::From<core::i32>(static_cast<core::i32>(d)); }
                if (expected == &core::TypeOf<core::i64>()) { return core::Variant::From<core::i64>(static_cast<core::i64>(d)); }
                if (expected == &core::TypeOf<core::u32>()) { return core::Variant::From<core::u32>(static_cast<core::u32>(d)); }
                if (expected == &core::TypeOf<core::u64>()) { return core::Variant::From<core::u64>(static_cast<core::u64>(d)); }
                return core::Variant::From<core::f64>(d);
            }
            case WREN_TYPE_STRING:
            {
                int length = 0;
                const char* bytes = wrenGetSlotBytes(vm, slot, &length);
                (void)expected; // engine String is UTF-8, like Wren strings
                return core::Variant::From<core::String>(core::String(core::StringView(
                    reinterpret_cast<const core::utf8char*>(bytes), static_cast<core::usize>(length))));
            }
            default:
                return core::Variant{};
        }
    }

    // Builds a Wren call signature "name(_,_,...)" with `argc` parameter slots.
    inline void BuildSignature(char* out, core::usize capacity, const char* name, core::usize argc)
    {
        core::usize pos = 0;
        for (const char* p = name; *p != '\0' && pos + 1 < capacity; ++p) { out[pos++] = *p; }
        if (pos + 1 < capacity) { out[pos++] = '('; }
        for (core::usize i = 0; i < argc && pos + 2 < capacity; ++i)
        {
            out[pos++] = '_';
            if (i + 1 < argc) { out[pos++] = ','; }
        }
        if (pos + 1 < capacity) { out[pos++] = ')'; }
        out[pos] = '\0';
    }

    // --- overload resolution (by argument type) ----------------------------
    // Is the value in arg slot `p+1` acceptable for parameter type `pt`?
    inline bool SlotMatchesParam(WrenVM* vm, int slot, const core::TypeInfo* pt)
    {
        switch (wrenGetSlotType(vm, slot))
        {
            case WREN_TYPE_FOREIGN:
            {
                const core::Variant* v = *static_cast<core::Variant**>(wrenGetSlotForeign(vm, slot));
                return core::IsDerivedFrom(v->Type(), pt); // exact, or object covariance
            }
            case WREN_TYPE_NUM:
                return pt == &core::TypeOf<core::f32>() || pt == &core::TypeOf<core::f64>()
                    || pt == &core::TypeOf<core::i32>() || pt == &core::TypeOf<core::i64>()
                    || pt == &core::TypeOf<core::u32>() || pt == &core::TypeOf<core::u64>();
            case WREN_TYPE_BOOL:
                return pt == &core::TypeOf<bool>();
            case WREN_TYPE_STRING:
                return pt == &core::TypeOf<core::String>() || pt == &core::TypeOf<core::String>();
            default:
                return false;
        }
    }

    // Among `type`'s methods sharing the bound method's name/arity/static-ness,
    // pick the first whose parameters match the actual argument slots. Falls
    // back to the bound method (e.g. when nothing matches better).
    inline const core::MethodInfo* ResolveOverload(const core::TypeInfo& type, const core::MethodInfo& bound,
                                                 WrenVM* vm, int argc)
    {
        for (core::usize i = 0; i < core::MethodCount(type); ++i)
        {
            const core::MethodInfo& m = core::MethodAt(type, i);
            if (m.isStatic != bound.isStatic || m.paramCount != static_cast<core::u32>(argc)
                || !NameEq(m.name, bound.name)) { continue; }
            bool match = true;
            for (int p = 0; p < argc; ++p)
            {
                if (!SlotMatchesParam(vm, p + 1, m.params[p].type())) { match = false; break; }
            }
            if (match) { return &m; }
        }
        return &bound;
    }

    // --- foreign-binding dispatch pool -------------------------------------
    inline constexpr int kMaxBindings = 256;
    inline constexpr int kMaxArgs = 8;

    enum class BindKind { Constructor, PropertyGet, PropertySet, Method };

    struct Binding
    {
        BindKind kind;
        const core::TypeInfo* type;
        const core::PropertyInfo* property;
        const core::MethodInfo* method;
    };

    Binding g_bindings[kMaxBindings];
    int g_bindingCount = 0;

    void Dispatch(WrenVM* vm, const Binding& binding); // defined below

    template <int I>
    void Trampoline(WrenVM* vm) { Dispatch(vm, g_bindings[I]); }

    WrenForeignMethodFn g_table[kMaxBindings];

    template <int... Is>
    void FillTable(std::integer_sequence<int, Is...>) { ((g_table[Is] = &Trampoline<Is>), ...); }

    // Fill the trampoline table once, on first use (a function-local static is
    // reliably initialized; a namespace-scope static initializer is not, under
    // GCC's module semantics).
    inline void EnsureTable()
    {
        static const bool ready = (FillTable(std::make_integer_sequence<int, kMaxBindings>{}), true);
        (void)ready;
    }

    // A foreign instance stores a POINTER to a heap-allocated Variant: Wren's
    // foreign storage isn't aligned to alignof(Variant) (max_align_t), so the
    // Variant itself can't live there. The pointer is properly aligned.
    inline core::Variant* SelfOf(WrenVM* vm)
    {
        return *static_cast<core::Variant**>(wrenGetSlotForeign(vm, 0));
    }

    inline void FinalizeVariant(void* data)
    {
        core::DefaultAllocator().Delete(*static_cast<core::Variant**>(data));
    }

    // Reserve a trampoline for a binding. Identical bindings (same kind/type/
    // member) dispatch identically and are VM-independent, so they're deduped and
    // share a slot - keeping the pool bounded by the distinct reflected surface
    // rather than the number of contexts created. Returns null if full.
    inline WrenForeignMethodFn Reserve(const Binding& binding)
    {
        EnsureTable();
        for (int i = 0; i < g_bindingCount; ++i)
        {
            const Binding& e = g_bindings[i];
            if (e.kind == binding.kind && e.type == binding.type
                && e.property == binding.property && e.method == binding.method)
            {
                return g_table[i];
            }
        }
        if (g_bindingCount >= kMaxBindings) { return nullptr; }
        const int slot = g_bindingCount++;
        g_bindings[slot] = binding;
        return g_table[slot];
    }

    // Defined after WrenContext (the user data holds a WrenContext*).
    [[nodiscard]] IScriptContext* OwningContext(WrenVM* vm);

    void Dispatch(WrenVM* vm, const Binding& binding)
    {
        // Every reflected call runs under its context: native facades resolve their
        // per-context services through CurrentScriptContext().
        ScriptCallScope scope(OwningContext(vm));
        switch (binding.kind)
        {
            case BindKind::Constructor:
            {
                const int argc = wrenGetSlotCount(vm) - 1;
                const core::ConstructorInfo* ctor = nullptr;
                for (core::usize i = 0; i < core::ConstructorCount(*binding.type); ++i)
                {
                    const core::ConstructorInfo& candidate = core::ConstructorAt(*binding.type, i);
                    if (static_cast<int>(candidate.paramCount) == argc) { ctor = &candidate; break; }
                }
                core::Variant args[kMaxArgs];
                for (int i = 0; i < argc && i < kMaxArgs; ++i)
                {
                    const core::TypeInfo* expected = (ctor != nullptr) ? ctor->params[i].type() : nullptr;
                    args[i] = MarshalIn(vm, i + 1, expected);
                }
                core::Result<core::Variant> created = core::Construct(
                    *binding.type, core::Span<core::Variant>{ args, static_cast<core::usize>(argc) });
                core::Variant* boxed = core::DefaultAllocator().New<core::Variant>(
                    created.HasValue() ? core::Move(created.Value()) : core::Variant{});
                void* data = wrenSetSlotNewForeign(vm, 0, 0, sizeof(core::Variant*));
                *static_cast<core::Variant**>(data) = boxed;
                break;
            }
            case BindKind::PropertyGet:
            {
                core::Instance instance = core::ToInstance(*SelfOf(vm));
                const core::Variant result = core::GetProperty(*binding.property, instance);
                MarshalOut(vm, 0, result);
                break;
            }
            case BindKind::PropertySet:
            {
                core::Instance instance = core::ToInstance(*SelfOf(vm));
                const core::Variant value = MarshalIn(vm, 1, binding.property->type);
                (void)core::SetProperty(*binding.property, instance, value);
                break;
            }
            case BindKind::Method:
            {
                const int argc = wrenGetSlotCount(vm) - 1;
                const core::MethodInfo* method = ResolveOverload(*binding.type, *binding.method, vm, argc);
                core::Variant args[kMaxArgs];
                for (int i = 0; i < argc && i < kMaxArgs; ++i)
                {
                    const core::TypeInfo* expected = (i < static_cast<int>(method->paramCount)) ? method->params[i].type() : nullptr;
                    args[i] = MarshalIn(vm, i + 1, expected);
                }
                const core::Span<core::Variant> argSpan{ args, static_cast<core::usize>(argc) };
                if (method->isStatic)
                {
                    const core::Result<core::Variant> r = core::InvokeStatic(*method, argSpan);
                    if (r.HasValue()) { MarshalOut(vm, 0, r.Value()); } else { wrenSetSlotNull(vm, 0); }
                }
                else
                {
                    const core::Result<core::Variant> r = core::InvokeMethod(*method, core::ToInstance(*SelfOf(vm)), argSpan);
                    if (r.HasValue()) { MarshalOut(vm, 0, r.Value()); } else { wrenSetSlotNull(vm, 0); }
                }
                break;
            }
        }
    }

    // --- signature parsing (Wren -> reflected member) ----------------------
    inline bool IsSetterSig(const char* sig)
    {
        for (const char* p = sig; p[0] != '\0'; ++p) { if (p[0] == '=' && p[1] == '(') { return true; } }
        return false;
    }
    inline bool HasParens(const char* sig)
    {
        for (const char* p = sig; *p != '\0'; ++p) { if (*p == '(') { return true; } }
        return false;
    }
    // Member name = signature up to the first '(' or '='.
    inline void MemberName(const char* sig, char* out, int capacity)
    {
        int n = 0;
        for (const char* p = sig; *p != '\0' && *p != '(' && *p != '=' && n < capacity - 1; ++p) { out[n++] = *p; }
        out[n] = '\0';
    }

    // Forward-declared so WrenContext's config can reference them; defined after.
    WrenForeignClassMethods BindForeignClass(WrenVM* vm, const char* module, const char* className);
    WrenForeignMethodFn BindForeignMethod(WrenVM* vm, const char* module, const char* className,
                                          bool isStatic, const char* signature);

    // A live instance of a script-defined Wren class. Holds a handle to the
    // object plus a strong reference to its owning context (keeping the VM alive),
    // and dispatches Invoke() by building the method's Wren call signature.
    class WrenScriptObject final : public ScriptObject
    {
    public:
        WrenScriptObject(core::RefPtr<IScriptContext> owner, WrenVM* vm, WrenHandle* instance) noexcept
            : m_owner(core::Move(owner)), m_vm(vm), m_instance(instance) {}

        ~WrenScriptObject() override
        {
            if (m_vm != nullptr && m_instance != nullptr) { wrenReleaseHandle(m_vm, m_instance); }
        }

        WrenScriptObject(const WrenScriptObject&) = delete;
        WrenScriptObject& operator=(const WrenScriptObject&) = delete;

        [[nodiscard]] core::Result<core::Variant> Invoke(core::StringView method, core::Span<core::Variant> args) override
        {
            const core::usize argc = args.Size();
            wrenEnsureSlots(m_vm, static_cast<int>(argc) + 1);
            wrenSetSlotHandle(m_vm, 0, m_instance); // receiver
            for (core::usize i = 0; i < argc; ++i) { MarshalOut(m_vm, static_cast<int>(i) + 1, args[i]); }

            const core::String name(method);
            char signature[96];
            BuildSignature(signature, sizeof(signature), CStr(name), argc);
            WrenHandle* call = wrenMakeCallHandle(m_vm, signature);
            const WrenInterpretResult result = wrenCall(m_vm, call);
            wrenReleaseHandle(m_vm, call);
            if (result != WREN_RESULT_SUCCESS) { return core::Err(core::ErrorCode::Internal); }
            return SlotToVariant(m_vm, 0);
        }

    private:
        core::RefPtr<IScriptContext> m_owner;
        WrenVM* m_vm;
        WrenHandle* m_instance;
    };

    class WrenContext final : public IScriptContext
    {
    public:
        explicit WrenContext(core::Span<const core::TypeInfo* const> types)
        {
            for (const core::TypeInfo* t : types) { m_types.PushBack(t); }

            WrenConfiguration config;
            wrenInitConfiguration(&config);
            config.writeFn = &OnWrite;
            config.errorFn = &OnError;
            config.bindForeignClassFn = &BindForeignClass;
            config.bindForeignMethodFn = &BindForeignMethod;
            m_vm = wrenNewVM(&config);
            wrenSetUserData(m_vm, this);   // OwningContext() maps a vm back to us

            m_module = core::String(reinterpret_cast<const core::utf8char*>("main"));
            GenerateForeignClasses();
        }

        ~WrenContext() override { if (m_vm != nullptr) { wrenFreeVM(m_vm); } }

        WrenContext(const WrenContext&) = delete;
        WrenContext& operator=(const WrenContext&) = delete;

        [[nodiscard]] const core::TypeInfo* FindType(const char* className) const
        {
            for (const core::TypeInfo* t : m_types)
            {
                if (t != nullptr && t->name != nullptr && NameEq(t->name, className)) { return t; }
            }
            return nullptr;
        }

        core::Status Load(core::StringView source, core::StringView chunkName) override
        {
            const core::String src(source);
            const core::String name(chunkName);
            const WrenInterpretResult result = wrenInterpret(m_vm, CStr(name), CStr(src));
            switch (result)
            {
                case WREN_RESULT_SUCCESS:       m_module = name; return core::Status{};
                case WREN_RESULT_COMPILE_ERROR: return core::Status{ core::ErrorCode::InvalidArgument };
                case WREN_RESULT_RUNTIME_ERROR: return core::Status{ core::ErrorCode::Internal };
            }
            return core::Status{ core::ErrorCode::Unknown };
        }

        void SetErrorHandler(IScriptErrorHandler* handler) override { m_errorHandler = handler; }

        void SetGlobal(core::StringView, const core::Variant&) override {}

        [[nodiscard]] core::Variant GetGlobal(core::StringView name) override
        {
            if (!HasVariable(name)) { return core::Variant{}; }
            const core::String nm(name);
            wrenEnsureSlots(m_vm, 1);
            wrenGetVariable(m_vm, CStr(m_module), CStr(nm), 0);
            return SlotToVariant(m_vm, 0);
        }

        [[nodiscard]] bool HasFunction(core::StringView name) const override { return HasVariable(name); }

        [[nodiscard]] core::Result<core::Variant> Call(core::StringView function, core::Span<core::Variant> args) override
        {
            if (!HasVariable(function)) { return core::Err(core::ErrorCode::NotFound); }
            const core::usize argc = args.Size();
            wrenEnsureSlots(m_vm, static_cast<int>(argc) + 1);
            const core::String nm(function);
            wrenGetVariable(m_vm, CStr(m_module), CStr(nm), 0);
            for (core::usize i = 0; i < argc; ++i) { MarshalOut(m_vm, static_cast<int>(i) + 1, args[i]); }

            char signature[64];
            BuildCallSignature(signature, sizeof(signature), argc);
            WrenHandle* handle = wrenMakeCallHandle(m_vm, signature);
            const WrenInterpretResult result = wrenCall(m_vm, handle);
            wrenReleaseHandle(m_vm, handle);
            if (result != WREN_RESULT_SUCCESS) { return core::Err(core::ErrorCode::Internal); }
            return SlotToVariant(m_vm, 0);
        }

        [[nodiscard]] core::RefPtr<ScriptObject> CreateInstance(
            core::StringView className, core::Span<core::Variant> args) override
        {
            if (!HasVariable(className)) { return nullptr; }

            const core::usize argc = args.Size();
            wrenEnsureSlots(m_vm, static_cast<int>(argc) + 1);
            const core::String cls(className);
            wrenGetVariable(m_vm, CStr(m_module), CStr(cls), 0); // class object -> slot 0
            for (core::usize i = 0; i < argc; ++i) { MarshalOut(m_vm, static_cast<int>(i) + 1, args[i]); }

            char signature[64];
            BuildSignature(signature, sizeof(signature), "new", argc);
            WrenHandle* call = wrenMakeCallHandle(m_vm, signature);
            const WrenInterpretResult result = wrenCall(m_vm, call);
            wrenReleaseHandle(m_vm, call);
            if (result != WREN_RESULT_SUCCESS) { return nullptr; }

            WrenHandle* instance = wrenGetSlotHandle(m_vm, 0);
            return core::RefPtr<ScriptObject>(core::MakeRef<WrenScriptObject>(
                core::DefaultAllocator(), core::RefPtr<IScriptContext>(this), m_vm, instance));
        }

    private:
        static void BuildCallSignature(char* out, core::usize capacity, core::usize argc)
        {
            core::usize pos = 0;
            for (const char* p = "call("; *p != '\0' && pos + 1 < capacity; ++p) { out[pos++] = *p; }
            for (core::usize i = 0; i < argc && pos + 2 < capacity; ++i)
            {
                out[pos++] = '_';
                if (i + 1 < argc) { out[pos++] = ','; }
            }
            if (pos + 1 < capacity) { out[pos++] = ')'; }
            out[pos] = '\0';
        }

        // Emits a `foreign class` per registered, constructible type and runs it
        // into the "main" module so user scripts can use the reflected types.
        void GenerateForeignClasses()
        {
            core::String src;
            for (const core::TypeInfo* t : m_types)
            {
                if (t == nullptr || core::ConstructorCount(*t) == 0) { continue; }
                AppendClass(src, *t);
            }
            if (!src.IsEmpty()) { (void)wrenInterpret(m_vm, "main", CStr(src)); }
        }

        static void AppendClass(core::String& src, const core::TypeInfo& type)
        {
            AppendAscii(src, "foreign class ");
            AppendAscii(src, type.name);
            AppendAscii(src, " {\n");
            for (core::usize i = 0; i < core::ConstructorCount(type); ++i)
            {
                const core::u32 arity = core::ConstructorAt(type, i).paramCount;
                AppendAscii(src, "  construct new(");
                for (core::u32 p = 0; p < arity; ++p)
                {
                    AppendAscii(src, "a");
                    AppendUint(src, p);
                    if (p + 1 < arity) { AppendAscii(src, ", "); }
                }
                AppendAscii(src, ") {}\n");
            }
            for (core::usize i = 0; i < core::PropertyCount(type); ++i)
            {
                const core::PropertyInfo& prop = core::PropertyAt(type, i);
                AppendAscii(src, "  foreign ");
                AppendAscii(src, prop.name);
                AppendAscii(src, "\n");
                AppendAscii(src, "  foreign ");
                AppendAscii(src, prop.name);
                AppendAscii(src, "=(value)\n");
            }
            for (core::usize i = 0; i < core::MethodCount(type); ++i)
            {
                const core::MethodInfo& method = core::MethodAt(type, i);
                // Wren overloads only by name+arity, so a same-(name,arity,static)
                // overload is emitted once; the binding picks the first match.
                bool duplicate = false;
                for (core::usize j = 0; j < i; ++j)
                {
                    const core::MethodInfo& earlier = core::MethodAt(type, j);
                    if (earlier.paramCount == method.paramCount &&
                        earlier.isStatic == method.isStatic && NameEq(earlier.name, method.name))
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) { continue; }

                AppendAscii(src, "  foreign ");
                if (method.isStatic) { AppendAscii(src, "static "); }
                AppendAscii(src, method.name);
                AppendAscii(src, "(");
                for (core::u32 p = 0; p < method.paramCount; ++p)
                {
                    AppendAscii(src, "a");
                    AppendUint(src, p);
                    if (p + 1 < method.paramCount) { AppendAscii(src, ", "); }
                }
                AppendAscii(src, ")\n");
            }
            AppendAscii(src, "}\n");
        }

        [[nodiscard]] bool HasVariable(core::StringView name) const
        {
            if (m_module.IsEmpty() || !wrenHasModule(m_vm, CStr(m_module))) { return false; }
            const core::String nm(name);
            return wrenHasVariable(m_vm, CStr(m_module), CStr(nm));
        }

        static void OnWrite(WrenVM*, const char* text) { WriteUtf8(&core::ConsoleWrite, text); }
        static void OnError(WrenVM* vm, WrenErrorType type, const char* module, int line, const char* message)
        {
            WrenContext* self = static_cast<WrenContext*>(wrenGetUserData(vm));
            if (self != nullptr && self->m_errorHandler != nullptr)
            {
                // Stack-trace frames follow a runtime error; surface the message kinds.
                if (type == WREN_ERROR_COMPILE || type == WREN_ERROR_RUNTIME)
                {
                    const core::StringView mod = (module != nullptr)
                        ? core::StringView(reinterpret_cast<const core::utf8char*>(module)) : core::StringView{};
                    const core::StringView msg = (message != nullptr)
                        ? core::StringView(reinterpret_cast<const core::utf8char*>(message)) : core::StringView{};
                    const ScriptError error{
                        (type == WREN_ERROR_COMPILE) ? ScriptErrorKind::Compile : ScriptErrorKind::Runtime,
                        mod, static_cast<core::i32>(line), msg };
                    self->m_errorHandler->OnError(error);
                }
                return;
            }
            WriteUtf8(&core::ConsoleWriteError, message);
        }

        WrenVM* m_vm = nullptr;
        IScriptErrorHandler* m_errorHandler = nullptr;
        core::String m_module;
        core::Array<const core::TypeInfo*> m_types;
    };

    // --- foreign bind callbacks (defined after WrenContext) ----------------
    WrenForeignClassMethods BindForeignClass(WrenVM* vm, const char*, const char* className)
    {
        WrenForeignClassMethods methods{};
        const WrenContext* ctx = static_cast<const WrenContext*>(wrenGetUserData(vm));
        const core::TypeInfo* type = (ctx != nullptr) ? ctx->FindType(className) : nullptr;
        if (type != nullptr && core::ConstructorCount(*type) > 0)
        {
            methods.allocate = Reserve(Binding{ BindKind::Constructor, type, nullptr, nullptr });
            methods.finalize = &FinalizeVariant;
        }
        return methods;
    }

    // First method on `type` matching name + static-ness (Wren tells us which).
    inline const core::MethodInfo* FindMethodMatching(const core::TypeInfo& type, const char* name, bool isStatic)
    {
        for (core::usize i = 0; i < core::MethodCount(type); ++i)
        {
            const core::MethodInfo& m = core::MethodAt(type, i);
            if (m.isStatic == isStatic && NameEq(m.name, name)) { return &m; }
        }
        return nullptr;
    }

    WrenForeignMethodFn BindForeignMethod(WrenVM* vm, const char*, const char* className,
                                          bool isStatic, const char* signature)
    {
        const WrenContext* ctx = static_cast<const WrenContext*>(wrenGetUserData(vm));
        const core::TypeInfo* type = (ctx != nullptr) ? ctx->FindType(className) : nullptr;
        if (type == nullptr) { return nullptr; }

        char name[64];
        MemberName(signature, name, sizeof(name));

        if (IsSetterSig(signature))
        {
            const core::PropertyInfo* prop = core::FindProperty(*type, name);
            return (prop != nullptr) ? Reserve(Binding{ BindKind::PropertySet, type, prop, nullptr }) : nullptr;
        }
        if (!HasParens(signature))
        {
            const core::PropertyInfo* prop = core::FindProperty(*type, name);
            return (prop != nullptr) ? Reserve(Binding{ BindKind::PropertyGet, type, prop, nullptr }) : nullptr;
        }
        const core::MethodInfo* method = FindMethodMatching(*type, name, isStatic);
        return (method != nullptr) ? Reserve(Binding{ BindKind::Method, type, nullptr, method }) : nullptr;
    }

    class WrenManager final : public IScriptManager
    {
    public:
        void RegisterType(const core::TypeInfo& type) override { m_types.PushBack(&type); }

        [[nodiscard]] core::RefPtr<IScriptContext> CreateContext() override
        {
            const core::Span<const core::TypeInfo* const> types{ m_types.Data(), m_types.Size() };
            return core::RefPtr<IScriptContext>(core::MakeRef<WrenContext>(core::DefaultAllocator(), types));
        }

        [[nodiscard]] ScriptCapabilities Capabilities() const override
        {
            return ScriptCapabilities::Fibers;   // first-class Fiber - the P2 scheduler backend
        }

    private:
        core::Array<const core::TypeInfo*> m_types;
    };

    [[nodiscard]] IScriptContext* OwningContext(WrenVM* vm)
    {
        return static_cast<WrenContext*>(wrenGetUserData(vm));
    }
}

export namespace draconic::script::wren
{
    [[nodiscard]] core::RefPtr<IScriptManager> CreateScriptManager()
    {
        return core::RefPtr<IScriptManager>(core::MakeRef<WrenManager>(core::DefaultAllocator()));
    }

    /// Registers Wren with the backend registry (scripting.md B1) - the ONE line that
    /// makes a language available; consumers resolve by extension, never by type.
    inline void RegisterWrenScriptBackend()
    {
        ScriptBackendDesc desc;
        desc.languageId = core::String(u8"wren");
        desc.displayName = core::String(u8"Wren");
        desc.fileExtensions.PushBack(core::String(u8"wren"));
        desc.create = []() { return CreateScriptManager(); };
        ScriptBackendRegistry::Get().Register(core::Move(desc));
    }
}
