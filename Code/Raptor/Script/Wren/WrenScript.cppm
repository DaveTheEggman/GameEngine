// Raptor::ScriptWren — Wren VM backend (raptor.script.wren).
//
// Implements Raptor::Script on Wren and binds reflected types into the VM:
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

export module raptor.script.wren;

import raptor.core;
import raptor.script;

namespace rc = raptor::core;

namespace raptor::script::wren
{
    inline const char* CStr(const rc::UTF8String& s) noexcept
    {
        return reinterpret_cast<const char*>(s.CStr());
    }

    inline void AppendAscii(rc::UTF8String& s, const char* text)
    {
        s.Append(rc::UTF8StringView(reinterpret_cast<const rc::utf8char*>(text)));
    }

    inline void AppendUint(rc::UTF8String& s, rc::u32 n)
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
        rc::usize i = 0;
        while (a[i] != '\0' && a[i] == b[i]) { ++i; }
        return a[i] == b[i];
    }

    inline void WriteUtf8(void (*sink)(rc::StringView), const char* text)
    {
        if (text != nullptr)
        {
            sink(rc::ToWide(rc::UTF8StringView(reinterpret_cast<const rc::utf8char*>(text))).AsView());
        }
    }

    // --- marshalling -------------------------------------------------------
    // Engine value -> Wren slot (primitives; non-primitives become null for now).
    inline void VariantToSlot(WrenVM* vm, int slot, const rc::Variant& value)
    {
        if (const bool* b = value.TryGet<bool>())       { wrenSetSlotBool(vm, slot, *b); return; }
        if (const rc::f64* d = value.TryGet<rc::f64>()) { wrenSetSlotDouble(vm, slot, *d); return; }
        if (const rc::f32* f = value.TryGet<rc::f32>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*f)); return; }
        if (const rc::i32* i = value.TryGet<rc::i32>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*i)); return; }
        if (const rc::i64* i = value.TryGet<rc::i64>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*i)); return; }
        if (const rc::u32* u = value.TryGet<rc::u32>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*u)); return; }
        if (const rc::u64* u = value.TryGet<rc::u64>()) { wrenSetSlotDouble(vm, slot, static_cast<double>(*u)); return; }
        if (const rc::String* s = value.TryGet<rc::String>())
        {
            const rc::UTF8String utf8 = rc::ToUTF8(s->AsView());
            wrenSetSlotBytes(vm, slot, CStr(utf8), utf8.Size());
            return;
        }
        if (const rc::UTF8String* s = value.TryGet<rc::UTF8String>())
        {
            wrenSetSlotBytes(vm, slot, CStr(*s), s->Size());
            return;
        }
        wrenSetSlotNull(vm, slot);
    }

    inline rc::Variant SlotToVariant(WrenVM* vm, int slot)
    {
        switch (wrenGetSlotType(vm, slot))
        {
            case WREN_TYPE_BOOL: return rc::Variant::From<bool>(wrenGetSlotBool(vm, slot));
            case WREN_TYPE_NUM:  return rc::Variant::From<rc::f64>(wrenGetSlotDouble(vm, slot));
            case WREN_TYPE_STRING:
            {
                int length = 0;
                const char* bytes = wrenGetSlotBytes(vm, slot, &length);
                return rc::Variant::From<rc::String>(rc::ToWide(
                    rc::UTF8StringView(reinterpret_cast<const rc::utf8char*>(bytes), static_cast<rc::usize>(length))));
            }
            default: return rc::Variant{};
        }
    }

    // Wren slot -> engine Variant of the expected reflected type (coerces Wren
    // numbers to the target scalar; foreign slots carry a Variant already).
    inline rc::Variant MarshalIn(WrenVM* vm, int slot, const rc::TypeInfo* expected)
    {
        switch (wrenGetSlotType(vm, slot))
        {
            case WREN_TYPE_FOREIGN:
                return **static_cast<rc::Variant**>(wrenGetSlotForeign(vm, slot)); // boxed Variant*
            case WREN_TYPE_BOOL:
                return rc::Variant::From<bool>(wrenGetSlotBool(vm, slot));
            case WREN_TYPE_NUM:
            {
                const double d = wrenGetSlotDouble(vm, slot);
                if (expected == &rc::TypeOf<rc::f32>()) { return rc::Variant::From<rc::f32>(static_cast<rc::f32>(d)); }
                if (expected == &rc::TypeOf<rc::i32>()) { return rc::Variant::From<rc::i32>(static_cast<rc::i32>(d)); }
                if (expected == &rc::TypeOf<rc::i64>()) { return rc::Variant::From<rc::i64>(static_cast<rc::i64>(d)); }
                if (expected == &rc::TypeOf<rc::u32>()) { return rc::Variant::From<rc::u32>(static_cast<rc::u32>(d)); }
                if (expected == &rc::TypeOf<rc::u64>()) { return rc::Variant::From<rc::u64>(static_cast<rc::u64>(d)); }
                return rc::Variant::From<rc::f64>(d);
            }
            case WREN_TYPE_STRING:
            {
                int length = 0;
                const char* bytes = wrenGetSlotBytes(vm, slot, &length);
                rc::String wide = rc::ToWide(rc::UTF8StringView(
                    reinterpret_cast<const rc::utf8char*>(bytes), static_cast<rc::usize>(length)));
                if (expected == &rc::TypeOf<rc::UTF8String>()) { return rc::Variant::From<rc::UTF8String>(rc::ToUTF8(wide.AsView())); }
                return rc::Variant::From<rc::String>(rc::Move(wide));
            }
            default:
                return rc::Variant{};
        }
    }

    // --- foreign-binding dispatch pool -------------------------------------
    inline constexpr int kMaxBindings = 256;
    inline constexpr int kMaxArgs = 8;

    enum class BindKind { Constructor, PropertyGet, PropertySet, Method };

    struct Binding
    {
        BindKind kind;
        const rc::TypeInfo* type;
        const rc::PropertyInfo* property;
        const rc::MethodInfo* method;
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
    inline rc::Variant* SelfOf(WrenVM* vm)
    {
        return *static_cast<rc::Variant**>(wrenGetSlotForeign(vm, 0));
    }

    inline void FinalizeVariant(void* data)
    {
        rc::DefaultAllocator().Delete(*static_cast<rc::Variant**>(data));
    }

    // Reserve a pool slot for a binding; returns its trampoline or null if full.
    inline WrenForeignMethodFn Reserve(const Binding& binding)
    {
        EnsureTable();
        if (g_bindingCount >= kMaxBindings) { return nullptr; }
        const int slot = g_bindingCount++;
        g_bindings[slot] = binding;
        return g_table[slot];
    }

    void Dispatch(WrenVM* vm, const Binding& binding)
    {
        switch (binding.kind)
        {
            case BindKind::Constructor:
            {
                const int argc = wrenGetSlotCount(vm) - 1;
                const rc::ConstructorInfo* ctor = nullptr;
                for (rc::usize i = 0; i < rc::ConstructorCount(*binding.type); ++i)
                {
                    const rc::ConstructorInfo& candidate = rc::ConstructorAt(*binding.type, i);
                    if (static_cast<int>(candidate.paramCount) == argc) { ctor = &candidate; break; }
                }
                rc::Variant args[kMaxArgs];
                for (int i = 0; i < argc && i < kMaxArgs; ++i)
                {
                    const rc::TypeInfo* expected = (ctor != nullptr) ? ctor->params[i].type() : nullptr;
                    args[i] = MarshalIn(vm, i + 1, expected);
                }
                rc::Result<rc::Variant> created = rc::Construct(
                    *binding.type, rc::Span<rc::Variant>{ args, static_cast<rc::usize>(argc) });
                rc::Variant* boxed = rc::DefaultAllocator().New<rc::Variant>(
                    created.HasValue() ? rc::Move(created.Value()) : rc::Variant{});
                void* data = wrenSetSlotNewForeign(vm, 0, 0, sizeof(rc::Variant*));
                *static_cast<rc::Variant**>(data) = boxed;
                break;
            }
            case BindKind::PropertyGet:
            {
                rc::Instance instance = rc::ToInstance(*SelfOf(vm));
                const rc::Variant result = rc::GetProperty(*binding.property, instance);
                VariantToSlot(vm, 0, result);
                break;
            }
            case BindKind::PropertySet:
            {
                rc::Instance instance = rc::ToInstance(*SelfOf(vm));
                const rc::Variant value = MarshalIn(vm, 1, binding.property->type);
                (void)rc::SetProperty(*binding.property, instance, value);
                break;
            }
            case BindKind::Method:
            {
                const rc::MethodInfo* method = binding.method;
                const int argc = wrenGetSlotCount(vm) - 1;
                rc::Variant args[kMaxArgs];
                for (int i = 0; i < argc && i < kMaxArgs; ++i)
                {
                    const rc::TypeInfo* expected = (i < static_cast<int>(method->paramCount)) ? method->params[i].type() : nullptr;
                    args[i] = MarshalIn(vm, i + 1, expected);
                }
                const rc::Span<rc::Variant> argSpan{ args, static_cast<rc::usize>(argc) };
                if (method->isStatic)
                {
                    const rc::Result<rc::Variant> r = rc::InvokeStatic(*method, argSpan);
                    if (r.HasValue()) { VariantToSlot(vm, 0, r.Value()); } else { wrenSetSlotNull(vm, 0); }
                }
                else
                {
                    const rc::Result<rc::Variant> r = rc::InvokeMethod(*method, rc::ToInstance(*SelfOf(vm)), argSpan);
                    if (r.HasValue()) { VariantToSlot(vm, 0, r.Value()); } else { wrenSetSlotNull(vm, 0); }
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

    class WrenContext final : public IScriptContext
    {
    public:
        explicit WrenContext(rc::Span<const rc::TypeInfo* const> types)
        {
            for (const rc::TypeInfo* t : types) { m_types.PushBack(t); }

            WrenConfiguration config;
            wrenInitConfiguration(&config);
            config.writeFn = &OnWrite;
            config.errorFn = &OnError;
            config.bindForeignClassFn = &BindForeignClass;
            config.bindForeignMethodFn = &BindForeignMethod;
            m_vm = wrenNewVM(&config);
            wrenSetUserData(m_vm, this);

            m_module = rc::UTF8String(reinterpret_cast<const rc::utf8char*>("main"));
            GenerateForeignClasses();
        }

        ~WrenContext() override { if (m_vm != nullptr) { wrenFreeVM(m_vm); } }

        WrenContext(const WrenContext&) = delete;
        WrenContext& operator=(const WrenContext&) = delete;

        [[nodiscard]] const rc::TypeInfo* FindType(const char* className) const
        {
            for (const rc::TypeInfo* t : m_types)
            {
                if (t != nullptr && t->name != nullptr && NameEq(t->name, className)) { return t; }
            }
            return nullptr;
        }

        rc::Status Load(rc::StringView source, rc::StringView chunkName) override
        {
            const rc::UTF8String src = rc::ToUTF8(source);
            const rc::UTF8String name = rc::ToUTF8(chunkName);
            const WrenInterpretResult result = wrenInterpret(m_vm, CStr(name), CStr(src));
            switch (result)
            {
                case WREN_RESULT_SUCCESS:       m_module = name; return rc::Status{};
                case WREN_RESULT_COMPILE_ERROR: return rc::Status{ rc::ErrorCode::InvalidArgument };
                case WREN_RESULT_RUNTIME_ERROR: return rc::Status{ rc::ErrorCode::Internal };
            }
            return rc::Status{ rc::ErrorCode::Unknown };
        }

        void SetGlobal(rc::StringView, const rc::Variant&) override {}

        [[nodiscard]] rc::Variant GetGlobal(rc::StringView name) override
        {
            if (!HasVariable(name)) { return rc::Variant{}; }
            const rc::UTF8String nm = rc::ToUTF8(name);
            wrenEnsureSlots(m_vm, 1);
            wrenGetVariable(m_vm, CStr(m_module), CStr(nm), 0);
            return SlotToVariant(m_vm, 0);
        }

        [[nodiscard]] bool HasFunction(rc::StringView name) const override { return HasVariable(name); }

        [[nodiscard]] rc::Result<rc::Variant> Call(rc::StringView function, rc::Span<rc::Variant> args) override
        {
            if (!HasVariable(function)) { return rc::Err(rc::ErrorCode::NotFound); }
            const rc::usize argc = args.Size();
            wrenEnsureSlots(m_vm, static_cast<int>(argc) + 1);
            const rc::UTF8String nm = rc::ToUTF8(function);
            wrenGetVariable(m_vm, CStr(m_module), CStr(nm), 0);
            for (rc::usize i = 0; i < argc; ++i) { VariantToSlot(m_vm, static_cast<int>(i) + 1, args[i]); }

            char signature[64];
            BuildCallSignature(signature, sizeof(signature), argc);
            WrenHandle* handle = wrenMakeCallHandle(m_vm, signature);
            const WrenInterpretResult result = wrenCall(m_vm, handle);
            wrenReleaseHandle(m_vm, handle);
            if (result != WREN_RESULT_SUCCESS) { return rc::Err(rc::ErrorCode::Internal); }
            return SlotToVariant(m_vm, 0);
        }

    private:
        static void BuildCallSignature(char* out, rc::usize capacity, rc::usize argc)
        {
            rc::usize pos = 0;
            for (const char* p = "call("; *p != '\0' && pos + 1 < capacity; ++p) { out[pos++] = *p; }
            for (rc::usize i = 0; i < argc && pos + 2 < capacity; ++i)
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
            rc::UTF8String src;
            for (const rc::TypeInfo* t : m_types)
            {
                if (t == nullptr || rc::ConstructorCount(*t) == 0) { continue; }
                AppendClass(src, *t);
            }
            if (!src.IsEmpty()) { (void)wrenInterpret(m_vm, "main", CStr(src)); }
        }

        static void AppendClass(rc::UTF8String& src, const rc::TypeInfo& type)
        {
            AppendAscii(src, "foreign class ");
            AppendAscii(src, type.name);
            AppendAscii(src, " {\n");
            for (rc::usize i = 0; i < rc::ConstructorCount(type); ++i)
            {
                const rc::u32 arity = rc::ConstructorAt(type, i).paramCount;
                AppendAscii(src, "  construct new(");
                for (rc::u32 p = 0; p < arity; ++p)
                {
                    AppendAscii(src, "a");
                    AppendUint(src, p);
                    if (p + 1 < arity) { AppendAscii(src, ", "); }
                }
                AppendAscii(src, ") {}\n");
            }
            for (rc::usize i = 0; i < rc::PropertyCount(type); ++i)
            {
                const rc::PropertyInfo& prop = rc::PropertyAt(type, i);
                AppendAscii(src, "  foreign ");
                AppendAscii(src, prop.name);
                AppendAscii(src, "\n");
                AppendAscii(src, "  foreign ");
                AppendAscii(src, prop.name);
                AppendAscii(src, "=(value)\n");
            }
            AppendAscii(src, "}\n");
        }

        [[nodiscard]] bool HasVariable(rc::StringView name) const
        {
            if (m_module.IsEmpty() || !wrenHasModule(m_vm, CStr(m_module))) { return false; }
            const rc::UTF8String nm = rc::ToUTF8(name);
            return wrenHasVariable(m_vm, CStr(m_module), CStr(nm));
        }

        static void OnWrite(WrenVM*, const char* text) { WriteUtf8(&rc::ConsoleWrite, text); }
        static void OnError(WrenVM*, WrenErrorType, const char*, int, const char* message)
        {
            WriteUtf8(&rc::ConsoleWriteError, message);
        }

        WrenVM* m_vm = nullptr;
        rc::UTF8String m_module;
        rc::Array<const rc::TypeInfo*> m_types;
    };

    // --- foreign bind callbacks (defined after WrenContext) ----------------
    WrenForeignClassMethods BindForeignClass(WrenVM* vm, const char*, const char* className)
    {
        WrenForeignClassMethods methods{};
        const WrenContext* ctx = static_cast<const WrenContext*>(wrenGetUserData(vm));
        const rc::TypeInfo* type = (ctx != nullptr) ? ctx->FindType(className) : nullptr;
        if (type != nullptr && rc::ConstructorCount(*type) > 0)
        {
            methods.allocate = Reserve(Binding{ BindKind::Constructor, type, nullptr, nullptr });
            methods.finalize = &FinalizeVariant;
        }
        return methods;
    }

    WrenForeignMethodFn BindForeignMethod(WrenVM* vm, const char*, const char* className,
                                          bool, const char* signature)
    {
        const WrenContext* ctx = static_cast<const WrenContext*>(wrenGetUserData(vm));
        const rc::TypeInfo* type = (ctx != nullptr) ? ctx->FindType(className) : nullptr;
        if (type == nullptr) { return nullptr; }

        char name[64];
        MemberName(signature, name, sizeof(name));

        if (IsSetterSig(signature))
        {
            const rc::PropertyInfo* prop = rc::FindProperty(*type, name);
            return (prop != nullptr) ? Reserve(Binding{ BindKind::PropertySet, type, prop, nullptr }) : nullptr;
        }
        if (!HasParens(signature))
        {
            const rc::PropertyInfo* prop = rc::FindProperty(*type, name);
            return (prop != nullptr) ? Reserve(Binding{ BindKind::PropertyGet, type, prop, nullptr }) : nullptr;
        }
        const rc::MethodInfo* method = rc::FindMethod(*type, name);
        return (method != nullptr) ? Reserve(Binding{ BindKind::Method, type, nullptr, method }) : nullptr;
    }

    class WrenManager final : public IScriptManager
    {
    public:
        void RegisterType(const rc::TypeInfo& type) override { m_types.PushBack(&type); }

        [[nodiscard]] rc::RefPtr<IScriptContext> CreateContext() override
        {
            const rc::Span<const rc::TypeInfo* const> types{ m_types.Data(), m_types.Size() };
            return rc::RefPtr<IScriptContext>(rc::MakeRef<WrenContext>(rc::DefaultAllocator(), types));
        }

    private:
        rc::Array<const rc::TypeInfo*> m_types;
    };
}

export namespace raptor::script::wren
{
    [[nodiscard]] rc::RefPtr<IScriptManager> CreateScriptManager()
    {
        return rc::RefPtr<IScriptManager>(rc::MakeRef<WrenManager>(rc::DefaultAllocator()));
    }
}
