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
import draconic.script.facades; // BehaviorFacadeNames() - the prelude's import list

namespace core = draconic::core;

namespace draconic::script::wren
{
    // The OPTIONAL Wren `Behavior` base class (scripting.md §3.3 coroutines), injected
    // into every behaviors module right after the facade prelude. A behavior opts in with
    // `class Mover is Behavior { ... }` to get startCoroutine(fn) / wait(seconds) /
    // waitUntil(fn); plain classes that do not extend it are untouched. The base owns the
    // instance's coroutine-id list and routes register/unregister to the manager's
    // host-side scheduler through two foreign methods (bound by name, below).
    inline constexpr core::StringView kBehaviorBaseSource =
        u8"class Behavior {\n"
        u8"  construct new(entity) {\n"
        u8"    _entity = entity\n"
        u8"    _drCoroutines = []\n"
        u8"  }\n"
        u8"  entity { _entity }\n"
        u8"  startCoroutine(fn) {\n"
        u8"    var fiber = Fiber.new(fn)\n"
        u8"    var w = fiber.call()\n"
        u8"    if (fiber.isDone) return -1\n"
        u8"    if (!(w is Num)) w = 0\n"
        u8"    var id = drRegisterCoroutine(fiber, w)\n"
        u8"    _drCoroutines.add(id)\n"
        u8"    return id\n"
        u8"  }\n"
        u8"  wait(seconds) { Fiber.yield(seconds) }\n"
        u8"  waitUntil(fn) {\n"
        u8"    while (!fn.call()) {\n"
        u8"      Fiber.yield(0)\n"
        u8"    }\n"
        u8"  }\n"
        u8"  foreign drRegisterCoroutine(fiber, w)\n"
        u8"  foreign drUnregisterCoroutine(id)\n"
        u8"  drCancelCoroutines() {\n"
        u8"    for (id in _drCoroutines) {\n"
        u8"      drUnregisterCoroutine(id)\n"
        u8"    }\n"
        u8"    _drCoroutines.clear()\n"
        u8"  }\n"
        u8"}\n";

    // Reflected classes live in the "main" module; behavior modules are separate, so the
    // module is framed with ONE prelude line importing the facade names (built from the
    // neutral BehaviorFacadeNames list) followed by the Behavior base. Appended to `out`.
    inline void AppendBehaviorPrelude(core::String& out)
    {
        out += u8"import \"main\" for ";
        const core::Span<const core::StringView> facades = BehaviorFacadeNames();
        const core::Span<const core::StringView> extras =
            ExtraFacadeNames(); // out-of-tree facades (e.g. Net)
        bool first = true;
        for (core::usize i = 0; i < facades.Size(); ++i)
        {
            if (!first)
            {
                out += u8", ";
            }
            out += facades[i];
            first = false;
        }
        for (core::usize i = 0; i < extras.Size(); ++i)
        {
            if (!first)
            {
                out += u8", ";
            }
            out += extras[i];
            first = false;
        }
        out += u8"\n";
        out += kBehaviorBaseSource;
    }

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
        if (n == 0)
        {
            buf[i++] = '0';
        }
        else
        {
            char tmp[16];
            int j = 0;
            while (n != 0)
            {
                tmp[j++] = static_cast<char>('0' + (n % 10));
                n /= 10;
            }
            while (j != 0)
            {
                buf[i++] = tmp[--j];
            }
        }
        buf[i] = '\0';
        AppendAscii(s, buf);
    }

    inline bool NameEq(const char* a, const char* b) noexcept
    {
        core::usize i = 0;
        while (a[i] != '\0' && a[i] == b[i])
        {
            ++i;
        }
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

    inline core::StringView AsciiView(const char* text) noexcept
    {
        return (text != nullptr) ? core::StringView(reinterpret_cast<const core::utf8char*>(text))
                                 : core::StringView{};
    }

    // The Wren call spelling of a method: `name(_,_,...)` with one `_` per parameter.
    inline core::String WrenCallSignatureOf(const char* name, core::u32 paramCount)
    {
        core::String sig(AsciiView(name));
        sig += u8"(";
        for (core::u32 i = 0; i < paramCount; ++i)
        {
            if (i != 0)
            {
                sig += u8",";
            }
            sig += u8"_";
        }
        sig += u8")";
        return sig;
    }

    // A script function argument (a Wren Fn/closure) -> a WrenScriptDelegate wrapping it,
    // as an object-mode Variant. Defined after WrenContext (it registers with the owning
    // context for lifetime); forward-declared so MarshalIn can wrap a delegate parameter.
    core::Variant WrapWrenDelegateArg(WrenVM* vm, int slot);

    // Engine value -> Wren slot for primitives; true if handled (slot untouched
    // and false otherwise, so the caller can try a foreign wrap).
    inline bool TryPrimitiveOut(WrenVM* vm, int slot, const core::Variant& value)
    {
        if (const bool* b = value.TryGet<bool>())
        {
            wrenSetSlotBool(vm, slot, *b);
            return true;
        }
        if (const core::f64* d = value.TryGet<core::f64>())
        {
            wrenSetSlotDouble(vm, slot, *d);
            return true;
        }
        if (const core::f32* f = value.TryGet<core::f32>())
        {
            wrenSetSlotDouble(vm, slot, static_cast<double>(*f));
            return true;
        }
        if (const core::i32* i = value.TryGet<core::i32>())
        {
            wrenSetSlotDouble(vm, slot, static_cast<double>(*i));
            return true;
        }
        if (const core::i64* i = value.TryGet<core::i64>())
        {
            wrenSetSlotDouble(vm, slot, static_cast<double>(*i));
            return true;
        }
        if (const core::u32* u = value.TryGet<core::u32>())
        {
            wrenSetSlotDouble(vm, slot, static_cast<double>(*u));
            return true;
        }
        if (const core::u64* u = value.TryGet<core::u64>())
        {
            wrenSetSlotDouble(vm, slot, static_cast<double>(*u));
            return true;
        }
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
        if (TryPrimitiveOut(vm, slot, value))
        {
            return;
        }

        const core::TypeInfo* type = value.Type();
        if (type != nullptr && type->name != nullptr && wrenHasModule(vm, kModule) &&
            wrenHasVariable(vm, kModule, type->name))
        {
            const int classSlot = wrenGetSlotCount(vm);
            wrenEnsureSlots(vm, classSlot + 1);
            wrenGetVariable(vm, kModule, type->name, classSlot);
            void* data = wrenSetSlotNewForeign(vm, slot, classSlot, sizeof(core::Variant*));
            *static_cast<core::Variant**>(data) =
                core::DefaultAllocator().New<core::Variant>(value);
            return;
        }
        wrenSetSlotNull(vm, slot);
    }

    inline core::Variant SlotToVariant(WrenVM* vm, int slot)
    {
        switch (wrenGetSlotType(vm, slot))
        {
        case WREN_TYPE_BOOL:
            return core::Variant::From<bool>(wrenGetSlotBool(vm, slot));
        case WREN_TYPE_NUM:
            return core::Variant::From<core::f64>(wrenGetSlotDouble(vm, slot));
        case WREN_TYPE_FOREIGN:
            return **static_cast<core::Variant**>(wrenGetSlotForeign(vm, slot));
        case WREN_TYPE_STRING:
        {
            int length = 0;
            const char* bytes = wrenGetSlotBytes(vm, slot, &length);
            return core::Variant::From<core::String>(core::String(core::StringView(
                reinterpret_cast<const core::utf8char*>(bytes), static_cast<core::usize>(length))));
        }
        default:
            return core::Variant{};
        }
    }

    // Wren slot -> engine Variant of the expected reflected type (coerces Wren
    // numbers to the target scalar; foreign slots carry a Variant already).
    inline core::Variant MarshalIn(WrenVM* vm, int slot, const core::TypeInfo* expected)
    {
        // A delegate parameter (RefPtr<IScriptDelegate>): the argument is a Wren fn/closure,
        // not a reflected foreign value - wrap it into a WrenScriptDelegate holding a handle
        // to the fn (grabbed regardless of slot type, the coroutine-registration recipe).
        if (expected != nullptr && core::IsDerivedFrom(expected, &IScriptDelegate::StaticType()))
        {
            return WrapWrenDelegateArg(vm, slot);
        }
        switch (wrenGetSlotType(vm, slot))
        {
        case WREN_TYPE_FOREIGN:
            return **static_cast<core::Variant**>(wrenGetSlotForeign(vm, slot)); // boxed Variant*
        case WREN_TYPE_BOOL:
            return core::Variant::From<bool>(wrenGetSlotBool(vm, slot));
        case WREN_TYPE_NUM:
        {
            const double d = wrenGetSlotDouble(vm, slot);
            if (expected == &core::TypeOf<core::f32>())
            {
                return core::Variant::From<core::f32>(static_cast<core::f32>(d));
            }
            if (expected == &core::TypeOf<core::i32>())
            {
                return core::Variant::From<core::i32>(static_cast<core::i32>(d));
            }
            if (expected == &core::TypeOf<core::i64>())
            {
                return core::Variant::From<core::i64>(static_cast<core::i64>(d));
            }
            if (expected == &core::TypeOf<core::u32>())
            {
                return core::Variant::From<core::u32>(static_cast<core::u32>(d));
            }
            if (expected == &core::TypeOf<core::u64>())
            {
                return core::Variant::From<core::u64>(static_cast<core::u64>(d));
            }
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
        for (const char* p = name; *p != '\0' && pos + 1 < capacity; ++p)
        {
            out[pos++] = *p;
        }
        if (pos + 1 < capacity)
        {
            out[pos++] = '(';
        }
        for (core::usize i = 0; i < argc && pos + 2 < capacity; ++i)
        {
            out[pos++] = '_';
            if (i + 1 < argc)
            {
                out[pos++] = ',';
            }
        }
        if (pos + 1 < capacity)
        {
            out[pos++] = ')';
        }
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
            return pt == &core::TypeOf<core::f32>() || pt == &core::TypeOf<core::f64>() ||
                   pt == &core::TypeOf<core::i32>() || pt == &core::TypeOf<core::i64>() ||
                   pt == &core::TypeOf<core::u32>() || pt == &core::TypeOf<core::u64>();
        case WREN_TYPE_BOOL:
            return pt == &core::TypeOf<bool>();
        case WREN_TYPE_STRING:
            return pt == &core::TypeOf<core::String>() || pt == &core::TypeOf<core::String>();
        default:
            // A Fn/closure (WREN_TYPE_UNKNOWN) matches a delegate parameter.
            return pt != nullptr && core::IsDerivedFrom(pt, &IScriptDelegate::StaticType());
        }
    }

    // Among `type`'s methods sharing the bound method's name/arity/static-ness,
    // pick the first whose parameters match the actual argument slots. Falls
    // back to the bound method (e.g. when nothing matches better).
    inline const core::MethodInfo*
    ResolveOverload(const core::TypeInfo& type, const core::MethodInfo& bound, WrenVM* vm, int argc)
    {
        for (core::usize i = 0; i < core::MethodCount(type); ++i)
        {
            const core::MethodInfo& m = core::MethodAt(type, i);
            if (m.isStatic != bound.isStatic || m.paramCount != static_cast<core::u32>(argc) ||
                !NameEq(m.name, bound.name))
            {
                continue;
            }
            bool match = true;
            for (int p = 0; p < argc; ++p)
            {
                if (!SlotMatchesParam(vm, p + 1, m.params[p].type()))
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                return &m;
            }
        }
        return &bound;
    }

    // --- foreign-binding dispatch pool -------------------------------------
    inline constexpr int kMaxBindings = 256;
    inline constexpr int kMaxArgs = 8;

    enum class BindKind
    {
        Constructor,
        PropertyGet,
        PropertySet,
        Method
    };

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
    void Trampoline(WrenVM* vm)
    {
        Dispatch(vm, g_bindings[I]);
    }

    WrenForeignMethodFn g_table[kMaxBindings];

    template <int... Is>
    void FillTable(std::integer_sequence<int, Is...>)
    {
        ((g_table[Is] = &Trampoline<Is>), ...);
    }

    // Fill the trampoline table once, on first use (a function-local static is
    // reliably initialized; a namespace-scope static initializer is not, under
    // GCC's module semantics).
    inline void EnsureTable()
    {
        static const bool ready =
            (FillTable(std::make_integer_sequence<int, kMaxBindings>{}), true);
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
            if (e.kind == binding.kind && e.type == binding.type &&
                e.property == binding.property && e.method == binding.method)
            {
                return g_table[i];
            }
        }
        if (g_bindingCount >= kMaxBindings)
        {
            return nullptr;
        }
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
                if (static_cast<int>(candidate.paramCount) == argc)
                {
                    ctor = &candidate;
                    break;
                }
            }
            core::Variant args[kMaxArgs];
            for (int i = 0; i < argc && i < kMaxArgs; ++i)
            {
                const core::TypeInfo* expected =
                    (ctor != nullptr) ? ctor->params[i].type() : nullptr;
                args[i] = MarshalIn(vm, i + 1, expected);
            }
            core::Result<core::Variant> created = core::Construct(
                *binding.type, core::Span<core::Variant>{args, static_cast<core::usize>(argc)});
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
            const core::MethodInfo* method =
                ResolveOverload(*binding.type, *binding.method, vm, argc);
            core::Variant args[kMaxArgs];
            for (int i = 0; i < argc && i < kMaxArgs; ++i)
            {
                const core::TypeInfo* expected =
                    (i < static_cast<int>(method->paramCount)) ? method->params[i].type() : nullptr;
                args[i] = MarshalIn(vm, i + 1, expected);
            }
            const core::Span<core::Variant> argSpan{args, static_cast<core::usize>(argc)};
            if (method->isStatic)
            {
                const core::Result<core::Variant> r = core::InvokeStatic(*method, argSpan);
                if (r.HasValue())
                {
                    MarshalOut(vm, 0, r.Value());
                }
                else
                {
                    wrenSetSlotNull(vm, 0);
                }
            }
            else
            {
                const core::Result<core::Variant> r =
                    core::InvokeMethod(*method, core::ToInstance(*SelfOf(vm)), argSpan);
                if (r.HasValue())
                {
                    MarshalOut(vm, 0, r.Value());
                }
                else
                {
                    wrenSetSlotNull(vm, 0);
                }
            }
            break;
        }
        }
    }

    // --- signature parsing (Wren -> reflected member) ----------------------
    inline bool IsSetterSig(const char* sig)
    {
        for (const char* p = sig; p[0] != '\0'; ++p)
        {
            if (p[0] == '=' && p[1] == '(')
            {
                return true;
            }
        }
        return false;
    }
    inline bool HasParens(const char* sig)
    {
        for (const char* p = sig; *p != '\0'; ++p)
        {
            if (*p == '(')
            {
                return true;
            }
        }
        return false;
    }
    // Member name = signature up to the first '(' or '='.
    inline void MemberName(const char* sig, char* out, int capacity)
    {
        int n = 0;
        for (const char* p = sig; *p != '\0' && *p != '(' && *p != '=' && n < capacity - 1; ++p)
        {
            out[n++] = *p;
        }
        out[n] = '\0';
    }

    // Forward-declared so WrenContext's config can reference them; defined after.
    WrenForeignClassMethods BindForeignClass(WrenVM* vm, const char* module, const char* className);
    WrenForeignMethodFn BindForeignMethod(WrenVM* vm, const char* module, const char* className,
                                          bool isStatic, const char* signature);

    // The coroutine primitives the Wren `Behavior` base declares as foreign methods
    // (scripting.md §3.3). Bound by name (independent of the reflected-type pool), they
    // route the fiber/id to the manager's host-side scheduler. Defined after WrenManager.
    void CoroutineRegisterForeign(WrenVM* vm);   // drRegisterCoroutine(fiber, wait) -> id
    void CoroutineUnregisterForeign(WrenVM* vm); // drUnregisterCoroutine(id)
    inline constexpr const char* kBehaviorClassName = "Behavior";

    // A live instance of a script-defined Wren class. Holds a handle to the
    // object plus a strong reference to its owning context (keeping the VM alive),
    // and dispatches Invoke() by building the method's Wren call signature.
    class WrenScriptObject final : public ScriptObject
    {
    public:
        WrenScriptObject(core::RefPtr<IScriptContext> owner, WrenVM* vm,
                         WrenHandle* instance) noexcept
            : m_owner(core::Move(owner)), m_vm(vm), m_instance(instance)
        {
        }

        ~WrenScriptObject() override
        {
            if (m_vm != nullptr && m_instance != nullptr)
            {
                wrenReleaseHandle(m_vm, m_instance);
            }
        }

        WrenScriptObject(const WrenScriptObject&) = delete;
        WrenScriptObject& operator=(const WrenScriptObject&) = delete;

        [[nodiscard]] core::Result<core::Variant> Invoke(core::StringView method,
                                                         core::Span<core::Variant> args) override
        {
            const core::usize argc = args.Size();
            wrenEnsureSlots(m_vm, static_cast<int>(argc) + 1);
            wrenSetSlotHandle(m_vm, 0, m_instance); // receiver
            for (core::usize i = 0; i < argc; ++i)
            {
                MarshalOut(m_vm, static_cast<int>(i) + 1, args[i]);
            }

            const core::String name(method);
            char signature[96];
            BuildSignature(signature, sizeof(signature), CStr(name), argc);
            WrenHandle* call = wrenMakeCallHandle(m_vm, signature);
            const WrenInterpretResult result = wrenCall(m_vm, call);
            wrenReleaseHandle(m_vm, call);
            if (result != WREN_RESULT_SUCCESS)
            {
                return core::Err(core::ErrorCode::Internal);
            }
            return SlotToVariant(m_vm, 0);
        }

    private:
        core::RefPtr<IScriptContext> m_owner;
        WrenVM* m_vm;
        WrenHandle* m_instance;
    };

    class WrenManager; // forward: WrenContext keeps its manager (the coroutine scheduler) alive
    class WrenScriptDelegate; // forward: WrenContext tracks its live delegates for teardown detach

    class WrenContext final : public IScriptContext
    {
    public:
        WrenContext(core::Span<const core::TypeInfo* const> types,
                    core::RefPtr<IScriptManager> manager)
            : m_manager(core::Move(manager))
        {
            for (const core::TypeInfo* t : types)
            {
                m_types.PushBack(t);
            }

            WrenConfiguration config;
            wrenInitConfiguration(&config);
            config.writeFn = &OnWrite;
            config.errorFn = &OnError;
            config.bindForeignClassFn = &BindForeignClass;
            config.bindForeignMethodFn = &BindForeignMethod;
            m_vm = wrenNewVM(&config);
            wrenSetUserData(m_vm, this); // OwningContext() maps a vm back to us

            m_module = core::String(reinterpret_cast<const core::utf8char*>("main"));
            GenerateForeignClasses();
        }

        ~WrenContext() override; // drops the VM's coroutines, then frees the VM

        WrenContext(const WrenContext&) = delete;
        WrenContext& operator=(const WrenContext&) = delete;

        // The owning manager (holds the host-side coroutine scheduler). Defined after
        // WrenManager; foreign coroutine callbacks route registration through it.
        [[nodiscard]] WrenManager& Manager() const noexcept;
        [[nodiscard]] WrenVM* Vm() const noexcept { return m_vm; }

        // A live WrenScriptDelegate tracks itself here so that, when this context's VM is
        // freed, we can detach each delegate (null its handle) BEFORE wrenFreeVM releases
        // the fn handles - releasing a handle after wrenFreeVM would be a use-after-free.
        // A delegate held only by native code is a safe no-op once detached.
        void RegisterDelegate(WrenScriptDelegate* delegate) { m_delegates.PushBack(delegate); }
        void UnregisterDelegate(WrenScriptDelegate* delegate)
        {
            for (core::usize i = 0; i < m_delegates.Size(); ++i)
            {
                if (m_delegates[i] == delegate)
                {
                    m_delegates.RemoveAt(i);
                    return;
                }
            }
        }

        [[nodiscard]] const core::TypeInfo* FindType(const char* className) const
        {
            for (const core::TypeInfo* t : m_types)
            {
                if (t != nullptr && t->name != nullptr && NameEq(t->name, className))
                {
                    return t;
                }
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
            case WREN_RESULT_SUCCESS:
                m_module = name;
                return core::Status{};
            case WREN_RESULT_COMPILE_ERROR:
                return core::Status{core::ErrorCode::InvalidArgument};
            case WREN_RESULT_RUNTIME_ERROR:
                return core::Status{core::ErrorCode::Internal};
            }
            return core::Status{core::ErrorCode::Unknown};
        }

        // Wren has no debug API (its debugger is deferred - script-debugger.md P1), so the
        // per-class `name` is unused: this stays BYTE-IDENTICAL to the old flat-assemble path -
        // the manager frames the facade prelude + coroutine base + concatenated sources, and
        // it loads as one chunk named `moduleName`, exactly as before. Defined out-of-line
        // (below WrenManager) because it dereferences the manager.
        core::Status LoadBehaviorModule(core::Span<const BehaviorModuleClass> classes,
                                        core::StringView moduleName) override;

        void SetErrorHandler(IScriptErrorHandler* handler) override { m_errorHandler = handler; }

        void SetGlobal(core::StringView, const core::Variant&) override {}

        [[nodiscard]] core::Variant GetGlobal(core::StringView name) override
        {
            if (!HasVariable(name))
            {
                return core::Variant{};
            }
            const core::String nm(name);
            wrenEnsureSlots(m_vm, 1);
            wrenGetVariable(m_vm, CStr(m_module), CStr(nm), 0);
            return SlotToVariant(m_vm, 0);
        }

        [[nodiscard]] bool HasFunction(core::StringView name) const override
        {
            return HasVariable(name);
        }

        [[nodiscard]] core::Result<core::Variant> Call(core::StringView function,
                                                       core::Span<core::Variant> args) override
        {
            if (!HasVariable(function))
            {
                return core::Err(core::ErrorCode::NotFound);
            }
            const core::usize argc = args.Size();
            wrenEnsureSlots(m_vm, static_cast<int>(argc) + 1);
            const core::String nm(function);
            wrenGetVariable(m_vm, CStr(m_module), CStr(nm), 0);
            for (core::usize i = 0; i < argc; ++i)
            {
                MarshalOut(m_vm, static_cast<int>(i) + 1, args[i]);
            }

            char signature[64];
            BuildCallSignature(signature, sizeof(signature), argc);
            WrenHandle* handle = wrenMakeCallHandle(m_vm, signature);
            const WrenInterpretResult result = wrenCall(m_vm, handle);
            wrenReleaseHandle(m_vm, handle);
            if (result != WREN_RESULT_SUCCESS)
            {
                return core::Err(core::ErrorCode::Internal);
            }
            return SlotToVariant(m_vm, 0);
        }

        [[nodiscard]] core::RefPtr<ScriptObject>
        CreateInstance(core::StringView className, core::Span<core::Variant> args) override
        {
            if (!HasVariable(className))
            {
                return nullptr;
            }

            const core::usize argc = args.Size();
            wrenEnsureSlots(m_vm, static_cast<int>(argc) + 1);
            const core::String cls(className);
            wrenGetVariable(m_vm, CStr(m_module), CStr(cls), 0); // class object -> slot 0
            for (core::usize i = 0; i < argc; ++i)
            {
                MarshalOut(m_vm, static_cast<int>(i) + 1, args[i]);
            }

            char signature[64];
            BuildSignature(signature, sizeof(signature), "new", argc);
            WrenHandle* call = wrenMakeCallHandle(m_vm, signature);
            const WrenInterpretResult result = wrenCall(m_vm, call);
            wrenReleaseHandle(m_vm, call);
            if (result != WREN_RESULT_SUCCESS)
            {
                return nullptr;
            }

            WrenHandle* instance = wrenGetSlotHandle(m_vm, 0);
            return core::RefPtr<ScriptObject>(core::MakeRef<WrenScriptObject>(
                core::DefaultAllocator(), core::RefPtr<IScriptContext>(this), m_vm, instance));
        }

    private:
        static void BuildCallSignature(char* out, core::usize capacity, core::usize argc)
        {
            core::usize pos = 0;
            for (const char* p = "call("; *p != '\0' && pos + 1 < capacity; ++p)
            {
                out[pos++] = *p;
            }
            for (core::usize i = 0; i < argc && pos + 2 < capacity; ++i)
            {
                out[pos++] = '_';
                if (i + 1 < argc)
                {
                    out[pos++] = ',';
                }
            }
            if (pos + 1 < capacity)
            {
                out[pos++] = ')';
            }
            out[pos] = '\0';
        }

        // Emits a `foreign class` per registered, constructible type and runs it
        // into the "main" module so user scripts can use the reflected types.
        void GenerateForeignClasses()
        {
            core::String src;
            for (const core::TypeInfo* t : m_types)
            {
                if (t == nullptr || core::ConstructorCount(*t) == 0)
                {
                    continue;
                }
                AppendClass(src, *t);
            }
            if (!src.IsEmpty())
            {
                (void)wrenInterpret(m_vm, "main", CStr(src));
            }
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
                    if (p + 1 < arity)
                    {
                        AppendAscii(src, ", ");
                    }
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
                if (duplicate)
                {
                    continue;
                }

                AppendAscii(src, "  foreign ");
                if (method.isStatic)
                {
                    AppendAscii(src, "static ");
                }
                AppendAscii(src, method.name);
                AppendAscii(src, "(");
                for (core::u32 p = 0; p < method.paramCount; ++p)
                {
                    AppendAscii(src, "a");
                    AppendUint(src, p);
                    if (p + 1 < method.paramCount)
                    {
                        AppendAscii(src, ", ");
                    }
                }
                AppendAscii(src, ")\n");
            }
            AppendAscii(src, "}\n");
        }

        [[nodiscard]] bool HasVariable(core::StringView name) const
        {
            if (m_module.IsEmpty() || !wrenHasModule(m_vm, CStr(m_module)))
            {
                return false;
            }
            const core::String nm(name);
            return wrenHasVariable(m_vm, CStr(m_module), CStr(nm));
        }

        static void OnWrite(WrenVM*, const char* text) { WriteUtf8(&core::ConsoleWrite, text); }
        static void OnError(WrenVM* vm, WrenErrorType type, const char* module, int line,
                            const char* message)
        {
            WrenContext* self = static_cast<WrenContext*>(wrenGetUserData(vm));
            if (self != nullptr && self->m_errorHandler != nullptr)
            {
                // Stack-trace frames follow a runtime error; surface the message kinds.
                if (type == WREN_ERROR_COMPILE || type == WREN_ERROR_RUNTIME)
                {
                    const core::StringView mod =
                        (module != nullptr)
                            ? core::StringView(reinterpret_cast<const core::utf8char*>(module))
                            : core::StringView{};
                    const core::StringView msg =
                        (message != nullptr)
                            ? core::StringView(reinterpret_cast<const core::utf8char*>(message))
                            : core::StringView{};
                    const ScriptError error{(type == WREN_ERROR_COMPILE) ? ScriptErrorKind::Compile
                                                                         : ScriptErrorKind::Runtime,
                                            mod, static_cast<core::i32>(line), msg};
                    self->m_errorHandler->OnError(error);
                }
                return;
            }
            WriteUtf8(&core::ConsoleWriteError, message);
        }

        // Detach every live delegate (defined after WrenScriptDelegate); called from the
        // destructor before wrenFreeVM.
        void DetachAllDelegates();

        WrenVM* m_vm = nullptr;
        IScriptErrorHandler* m_errorHandler = nullptr;
        core::String m_module;
        core::Array<const core::TypeInfo*> m_types;
        core::RefPtr<IScriptManager> m_manager; // keeps the manager (scheduler) alive
        core::Array<WrenScriptDelegate*>
            m_delegates; // live delegates (non-owning; detached on teardown)
    };

    // --- foreign bind callbacks (defined after WrenContext) ----------------
    WrenForeignClassMethods BindForeignClass(WrenVM* vm, const char*, const char* className)
    {
        WrenForeignClassMethods methods{};
        const WrenContext* ctx = static_cast<const WrenContext*>(wrenGetUserData(vm));
        const core::TypeInfo* type = (ctx != nullptr) ? ctx->FindType(className) : nullptr;
        if (type != nullptr && core::ConstructorCount(*type) > 0)
        {
            methods.allocate = Reserve(Binding{BindKind::Constructor, type, nullptr, nullptr});
            methods.finalize = &FinalizeVariant;
        }
        return methods;
    }

    // First method on `type` matching name + static-ness (Wren tells us which).
    inline const core::MethodInfo* FindMethodMatching(const core::TypeInfo& type, const char* name,
                                                      bool isStatic)
    {
        for (core::usize i = 0; i < core::MethodCount(type); ++i)
        {
            const core::MethodInfo& m = core::MethodAt(type, i);
            if (m.isStatic == isStatic && NameEq(m.name, name))
            {
                return &m;
            }
        }
        return nullptr;
    }

    WrenForeignMethodFn BindForeignMethod(WrenVM* vm, const char*, const char* className,
                                          bool isStatic, const char* signature)
    {
        char name[64];
        MemberName(signature, name, sizeof(name));

        // The `Behavior` base's coroutine primitives (a plain Wren class with foreign
        // methods - not a reflected type), bound by declaring-class name + method name.
        if (NameEq(className, kBehaviorClassName))
        {
            if (NameEq(name, "drRegisterCoroutine"))
            {
                return &CoroutineRegisterForeign;
            }
            if (NameEq(name, "drUnregisterCoroutine"))
            {
                return &CoroutineUnregisterForeign;
            }
        }

        const WrenContext* ctx = static_cast<const WrenContext*>(wrenGetUserData(vm));
        const core::TypeInfo* type = (ctx != nullptr) ? ctx->FindType(className) : nullptr;
        if (type == nullptr)
        {
            return nullptr;
        }

        if (IsSetterSig(signature))
        {
            const core::PropertyInfo* prop = core::FindProperty(*type, name);
            return (prop != nullptr) ? Reserve(Binding{BindKind::PropertySet, type, prop, nullptr})
                                     : nullptr;
        }
        if (!HasParens(signature))
        {
            const core::PropertyInfo* prop = core::FindProperty(*type, name);
            return (prop != nullptr) ? Reserve(Binding{BindKind::PropertyGet, type, prop, nullptr})
                                     : nullptr;
        }
        const core::MethodInfo* method = FindMethodMatching(*type, name, isStatic);
        return (method != nullptr) ? Reserve(Binding{BindKind::Method, type, nullptr, method})
                                   : nullptr;
    }

    class WrenManager final : public IScriptManager
    {
    public:
        ~WrenManager() override
        {
            // Every coroutine belongs to a context's VM, and a context holds a strong
            // ref to this manager - so by the time we're destroyed all contexts (and
            // their VMs) are gone and each already called ForgetCoroutinesForVm. The
            // list is empty here; drop it without touching any freed handle.
            m_coroutines.Clear();
        }

        void RegisterType(const core::TypeInfo& type) override { m_types.PushBack(&type); }

        [[nodiscard]] core::RefPtr<IScriptContext> CreateContext() override
        {
            const core::Span<const core::TypeInfo* const> types{m_types.Data(), m_types.Size()};
            return core::RefPtr<IScriptContext>(core::MakeRef<WrenContext>(
                core::DefaultAllocator(), types, core::RefPtr<IScriptManager>(this)));
        }

        [[nodiscard]] ScriptCapabilities Capabilities() const override
        {
            // Wren fibers back the coroutine scheduler; fn handles back the delegate seam.
            return ScriptCapabilities::Coroutines | ScriptCapabilities::Delegates;
        }

        // The ACTUAL Wren-callable surface: one entry per constructible reflected type (the
        // foreign classes GenerateForeignClasses emits), with Wren-spelled member signatures.
        [[nodiscard]] core::Array<ScriptApiType> DescribeBoundApi() const override
        {
            core::Array<ScriptApiType> result;
            for (const core::TypeInfo* t : m_types)
            {
                if (t == nullptr || t->name == nullptr)
                {
                    continue;
                }
                if (core::ConstructorCount(*t) == 0)
                {
                    continue;
                } // no foreign class emitted
                ScriptApiType api;
                api.scriptName = core::String(AsciiView(t->name));
                api.isNamespace = false;
                for (core::usize i = 0; i < core::PropertyCount(*t); ++i)
                {
                    const core::PropertyInfo& p = core::PropertyAt(*t, i);
                    ScriptApiMember member;
                    member.name = core::String(AsciiView(p.name));
                    member.signature = member.name; // Wren getter/setter share the bare name
                    member.kind = ScriptApiMemberKind::Property;
                    api.members.PushBack(core::Move(member));
                }
                for (core::usize i = 0; i < core::MethodCount(*t); ++i)
                {
                    const core::MethodInfo& m = core::MethodAt(*t, i);
                    ScriptApiMember member;
                    member.name = core::String(AsciiView(m.name));
                    member.signature = WrenCallSignatureOf(m.name, m.paramCount);
                    member.isStatic = m.isStatic;
                    member.kind = ScriptApiMemberKind::Method;
                    api.members.PushBack(core::Move(member));
                }
                result.PushBack(core::Move(api));
            }
            return result;
        }

        // The Wren behavior module: the facade `import "main" for ...` prelude + the
        // coroutine Behavior base, then the concatenated class sources. This is the ONLY
        // place the Wren behavior-module syntax lives (scripting.md §7.5).
        [[nodiscard]] core::String
        AssembleBehaviorModuleSource(core::Span<const core::StringView> classSources) const override
        {
            core::String moduleSource;
            AppendBehaviorPrelude(moduleSource);
            for (const core::StringView& source : classSources)
            {
                moduleSource += source;
                moduleSource += u8"\n";
            }
            return moduleSource;
        }

        // ---- the host-side coroutine scheduler (Wren fibers) ----

        /// A live coroutine: the fiber handle we own (release on drop), its VM, and the
        /// seconds still to wait before the next resume.
        struct WrenCoroutine
        {
            core::i32 id = 0;
            WrenVM* vm = nullptr;
            WrenHandle* fiber = nullptr;
            core::f64 wait = 0.0;
        };

        /// Foreign `__registerCoroutine`: takes ownership of the fiber handle, stores it
        /// with its initial wait, returns the id the Behavior base records.
        [[nodiscard]] core::i32 RegisterCoroutine(WrenVM* vm, WrenHandle* fiber, core::f64 wait)
        {
            const core::i32 id = ++m_nextCoroutineId;
            m_coroutines.PushBack(WrenCoroutine{id, vm, fiber, wait});
            return id;
        }

        /// Foreign `__unregisterCoroutine`: cancel by id (releases the fiber handle).
        void UnregisterCoroutine(core::i32 id)
        {
            for (core::usize i = 0; i < m_coroutines.Size(); ++i)
            {
                if (m_coroutines[i].id == id)
                {
                    if (m_coroutines[i].fiber != nullptr)
                    {
                        wrenReleaseHandle(m_coroutines[i].vm, m_coroutines[i].fiber);
                    }
                    m_coroutines.RemoveAt(i);
                    return;
                }
            }
        }

        /// A context's VM is being freed: drop its coroutines WITHOUT releasing the
        /// fiber handles (wrenFreeVM frees them - releasing here would double-free).
        void ForgetCoroutinesForVm(WrenVM* vm)
        {
            for (core::usize i = m_coroutines.Size(); i-- > 0;)
            {
                if (m_coroutines[i].vm == vm)
                {
                    m_coroutines.RemoveAt(i);
                }
            }
        }

        void AdvanceCoroutines(core::f64 deltaSeconds) override
        {
            // Snapshot the due ids first: a resumed fiber may start MORE coroutines
            // (append) - those are not advanced this frame - and by resuming through the
            // id (re-found after the call) a nested cancel can never touch a dead entry.
            for (WrenCoroutine& co : m_coroutines)
            {
                co.wait -= deltaSeconds;
            }
            m_dueScratch.Clear();
            for (const WrenCoroutine& co : m_coroutines)
            {
                if (co.wait <= kDueEpsilon)
                {
                    m_dueScratch.PushBack(co.id);
                }
            }
            for (const core::i32 id : m_dueScratch)
            {
                const core::i32 index = FindCoroutineIndex(id);
                if (index < 0)
                {
                    continue;
                }
                WrenVM* vm = m_coroutines[static_cast<core::usize>(index)].vm;
                WrenHandle* fiber = m_coroutines[static_cast<core::usize>(index)].fiber;

                // Resume: fiber.call(dt) runs it to its next Fiber.yield(seconds); the
                // yielded number lands in slot 0 as the next wait.
                wrenEnsureSlots(vm, 2);
                wrenSetSlotHandle(vm, 0, fiber);
                wrenSetSlotDouble(vm, 1, deltaSeconds);
                WrenHandle* call = wrenMakeCallHandle(vm, "call(_)");
                const WrenInterpretResult result = wrenCall(vm, call);
                wrenReleaseHandle(vm, call);

                bool drop = false;
                core::f64 nextWait = 0.0;
                if (result != WREN_RESULT_SUCCESS)
                {
                    drop = true; // the fiber faulted; the error already went to the sink
                }
                else
                {
                    if (wrenGetSlotType(vm, 0) == WREN_TYPE_NUM)
                    {
                        nextWait = wrenGetSlotDouble(vm, 0);
                    }
                    // isDone getter (reuses the slots we just read from).
                    wrenEnsureSlots(vm, 1);
                    wrenSetSlotHandle(vm, 0, fiber);
                    WrenHandle* done = wrenMakeCallHandle(vm, "isDone");
                    if (wrenCall(vm, done) == WREN_RESULT_SUCCESS &&
                        wrenGetSlotType(vm, 0) == WREN_TYPE_BOOL && wrenGetSlotBool(vm, 0))
                    {
                        drop = true;
                    }
                    wrenReleaseHandle(vm, done);
                }

                const core::i32 after = FindCoroutineIndex(id);
                if (after < 0)
                {
                    continue;
                } // a nested cancel already removed it
                if (drop)
                {
                    wrenReleaseHandle(m_coroutines[static_cast<core::usize>(after)].vm,
                                      m_coroutines[static_cast<core::usize>(after)].fiber);
                    m_coroutines.RemoveAt(static_cast<core::usize>(after));
                }
                else
                {
                    m_coroutines[static_cast<core::usize>(after)].wait = nextWait;
                }
            }
        }

        void CancelCoroutinesFor(ScriptObject& instance) override
        {
            // The Behavior base owns its own id list; asking it to cancel routes back
            // through drUnregisterCoroutine (which releases each fiber). Missing method
            // (a non-Behavior instance) just returns an error - a safe no-op.
            (void)instance.Invoke(u8"drCancelCoroutines", core::Span<core::Variant>{});
        }

    private:
        [[nodiscard]] core::i32 FindCoroutineIndex(core::i32 id) const
        {
            for (core::usize i = 0; i < m_coroutines.Size(); ++i)
            {
                if (m_coroutines[i].id == id)
                {
                    return static_cast<core::i32>(i);
                }
            }
            return -1;
        }

        static constexpr core::f64 kDueEpsilon = 1e-4;

        core::Array<const core::TypeInfo*> m_types;
        core::Array<WrenCoroutine> m_coroutines;
        core::Array<core::i32> m_dueScratch; // reused per-frame due-id snapshot
        core::i32 m_nextCoroutineId = 0;
    };

    // ---- script delegate (a Wren fn held as a native callback) ----

    // Wraps a Wren fn/closure handle so native code can call back into script through the
    // neutral IScriptDelegate seam. Non-owning of its context (holding it strongly would
    // cycle: VM -> foreign object -> delegate -> context -> VM). Instead it registers with
    // the context, which detaches it on VM teardown - a delegate outliving its context is a
    // safe no-op. The fn handle keeps the closure alive across Wren GC (the GC-safe promise).
    class WrenScriptDelegate final : public IScriptDelegate
    {
    public:
        WrenScriptDelegate(WrenContext* context, WrenHandle* fn) noexcept
            : m_context(context), m_fn(fn)
        {
            if (m_context != nullptr)
            {
                m_context->RegisterDelegate(this);
            }
        }

        ~WrenScriptDelegate() override
        {
            if (m_context != nullptr)
            {
                m_context->UnregisterDelegate(this);
                if (m_fn != nullptr)
                {
                    wrenReleaseHandle(m_context->Vm(), m_fn);
                }
            }
        }

        WrenScriptDelegate(const WrenScriptDelegate&) = delete;
        WrenScriptDelegate& operator=(const WrenScriptDelegate&) = delete;

        // The VM is being freed (wrenFreeVM releases the fn handle itself): drop our
        // references without touching them, so the destructor becomes a no-op.
        void Detach() noexcept
        {
            m_context = nullptr;
            m_fn = nullptr;
        }

        [[nodiscard]] core::Result<core::Variant> Invoke(core::Span<core::Variant> args) override
        {
            if (m_context == nullptr || m_fn == nullptr)
            {
                return core::Err(core::ErrorCode::Internal);
            }
            WrenVM* vm = m_context->Vm();
            const core::usize argc = args.Size();
            wrenEnsureSlots(vm, static_cast<int>(argc) + 1);
            wrenSetSlotHandle(vm, 0, m_fn); // the receiver is the fn itself
            for (core::usize i = 0; i < argc; ++i)
            {
                MarshalOut(vm, static_cast<int>(i) + 1, args[i]);
            }

            char signature[64];
            BuildSignature(signature, sizeof(signature), "call", argc);
            WrenHandle* call = wrenMakeCallHandle(vm, signature);
            const WrenInterpretResult result = wrenCall(vm, call);
            wrenReleaseHandle(vm, call);
            if (result != WREN_RESULT_SUCCESS)
            {
                return core::Err(core::ErrorCode::Internal);
            }
            return SlotToVariant(vm, 0);
        }

    private:
        WrenContext* m_context;
        WrenHandle* m_fn;
    };

    core::Variant WrapWrenDelegateArg(WrenVM* vm, int slot)
    {
        WrenContext* context = static_cast<WrenContext*>(OwningContext(vm));
        // A Fn is not a reflected foreign type; grab its handle regardless of slot type.
        WrenHandle* fn = wrenGetSlotHandle(vm, slot);
        core::RefPtr<IScriptDelegate> delegate(
            core::MakeRef<WrenScriptDelegate>(core::DefaultAllocator(), context, fn));
        return core::Variant::From(delegate);
    }

    void WrenContext::DetachAllDelegates()
    {
        for (WrenScriptDelegate* delegate : m_delegates)
        {
            delegate->Detach();
        }
        m_delegates.Clear();
    }

    // ---- coroutine foreign callbacks (defined after WrenManager) ----

    core::Status WrenContext::LoadBehaviorModule(core::Span<const BehaviorModuleClass> classes,
                                                 core::StringView moduleName)
    {
        core::Array<core::StringView> sources;
        sources.Reserve(classes.Size());
        for (const BehaviorModuleClass& entry : classes)
        {
            sources.PushBack(entry.source);
        }
        const core::String moduleSource = Manager().AssembleBehaviorModuleSource(
            core::Span<const core::StringView>{sources.Data(), sources.Size()});
        return Load(moduleSource.AsView(), moduleName);
    }

    WrenManager& WrenContext::Manager() const noexcept
    {
        return *static_cast<WrenManager*>(m_manager.Get());
    }

    WrenContext::~WrenContext()
    {
        if (m_vm != nullptr)
        {
            // Detach delegates and drop this VM's coroutines BEFORE freeing it (the manager
            // outlives us - we hold a strong ref); wrenFreeVM then frees the fn/fiber handles.
            DetachAllDelegates();
            Manager().ForgetCoroutinesForVm(m_vm);
            wrenFreeVM(m_vm);
        }
    }

    void CoroutineRegisterForeign(WrenVM* vm)
    {
        WrenContext* ctx = static_cast<WrenContext*>(wrenGetUserData(vm));
        // A Fiber is not a reflected foreign type, so grab its handle regardless of the
        // slot type (the documented recipe). Slot 2 is the initial wait (seconds).
        WrenHandle* fiber = wrenGetSlotHandle(vm, 1);
        const core::f64 wait =
            (wrenGetSlotType(vm, 2) == WREN_TYPE_NUM) ? wrenGetSlotDouble(vm, 2) : 0.0;
        const core::i32 id = ctx->Manager().RegisterCoroutine(vm, fiber, wait);
        wrenSetSlotDouble(vm, 0, static_cast<double>(id));
    }

    void CoroutineUnregisterForeign(WrenVM* vm)
    {
        WrenContext* ctx = static_cast<WrenContext*>(wrenGetUserData(vm));
        const core::i32 id = (wrenGetSlotType(vm, 1) == WREN_TYPE_NUM)
                                 ? static_cast<core::i32>(wrenGetSlotDouble(vm, 1))
                                 : -1;
        ctx->Manager().UnregisterCoroutine(id);
        wrenSetSlotNull(vm, 0);
    }

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
