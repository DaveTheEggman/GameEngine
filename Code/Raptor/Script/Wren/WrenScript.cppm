// Raptor::ScriptWren — Wren VM backend (raptor.script.wren).
//
// Implements Raptor::Script's IScriptManager / IScriptContext on the Wren VM.
// A WrenContext owns one WrenVM (Wren's VM is the isolated environment): Load()
// runs source, GetGlobal reads a module variable, Call invokes a callable
// global (a Fn), with primitive Variant <-> Wren-slot marshalling. Reflection-
// driven foreign-class binding (exposing reflected types to Wren) is next.

module;
#include "Core/Prelude.h"
#include "WrenInclude.h"

export module raptor.script.wren;

import raptor.core;
import raptor.script;

namespace rc = raptor::core;

namespace raptor::script::wren
{
    // Null-terminated UTF-8 view of a UTF8String for the Wren C API.
    inline const char* CStr(const rc::UTF8String& s) noexcept
    {
        return reinterpret_cast<const char*>(s.CStr());
    }

    inline void WriteUtf8(void (*sink)(rc::StringView), const char* text)
    {
        if (text != nullptr)
        {
            sink(rc::ToWide(rc::UTF8StringView(reinterpret_cast<const rc::utf8char*>(text))).AsView());
        }
    }

    // --- Variant <-> Wren slot (primitives) --------------------------------
    inline void VariantToSlot(WrenVM* vm, int slot, const rc::Variant& value)
    {
        if (const bool* b = value.TryGet<bool>())        { wrenSetSlotBool(vm, slot, *b); return; }
        if (const rc::f64* d = value.TryGet<rc::f64>())  { wrenSetSlotDouble(vm, slot, *d); return; }
        if (const rc::f32* f = value.TryGet<rc::f32>())  { wrenSetSlotDouble(vm, slot, static_cast<double>(*f)); return; }
        if (const rc::i32* i = value.TryGet<rc::i32>())  { wrenSetSlotDouble(vm, slot, static_cast<double>(*i)); return; }
        if (const rc::i64* i = value.TryGet<rc::i64>())  { wrenSetSlotDouble(vm, slot, static_cast<double>(*i)); return; }
        if (const rc::u32* u = value.TryGet<rc::u32>())  { wrenSetSlotDouble(vm, slot, static_cast<double>(*u)); return; }
        if (const rc::u64* u = value.TryGet<rc::u64>())  { wrenSetSlotDouble(vm, slot, static_cast<double>(*u)); return; }
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
        wrenSetSlotNull(vm, slot); // empty/object (objects: later, via foreign classes)
    }

    inline rc::Variant SlotToVariant(WrenVM* vm, int slot)
    {
        switch (wrenGetSlotType(vm, slot))
        {
            case WREN_TYPE_BOOL:
                return rc::Variant::From<bool>(wrenGetSlotBool(vm, slot));
            case WREN_TYPE_NUM:
                return rc::Variant::From<rc::f64>(wrenGetSlotDouble(vm, slot));
            case WREN_TYPE_STRING:
            {
                int length = 0;
                const char* bytes = wrenGetSlotBytes(vm, slot, &length);
                return rc::Variant::From<rc::String>(rc::ToWide(
                    rc::UTF8StringView(reinterpret_cast<const rc::utf8char*>(bytes), static_cast<rc::usize>(length))));
            }
            default:
                return rc::Variant{}; // NULL / LIST / MAP / FOREIGN -> empty for now
        }
    }

    // Builds a Wren call signature "call(_,_,...)" for `argc` arguments.
    inline void BuildCallSignature(char* out, rc::usize capacity, rc::usize argc)
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

    class WrenContext final : public IScriptContext
    {
    public:
        WrenContext()
        {
            WrenConfiguration config;
            wrenInitConfiguration(&config);
            config.writeFn = &OnWrite;
            config.errorFn = &OnError;
            m_vm = wrenNewVM(&config);
        }

        ~WrenContext() override
        {
            if (m_vm != nullptr) { wrenFreeVM(m_vm); }
        }

        WrenContext(const WrenContext&) = delete;
        WrenContext& operator=(const WrenContext&) = delete;

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

        // Wren has no host API to set a module variable; data flows into scripts
        // via Call arguments (or, later, foreign objects). No-op here.
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
            wrenGetVariable(m_vm, CStr(m_module), CStr(nm), 0); // the callable -> receiver slot 0
            for (rc::usize i = 0; i < argc; ++i)
            {
                VariantToSlot(m_vm, static_cast<int>(i) + 1, args[i]);
            }

            char signature[64];
            BuildCallSignature(signature, sizeof(signature), argc);
            WrenHandle* handle = wrenMakeCallHandle(m_vm, signature);
            const WrenInterpretResult result = wrenCall(m_vm, handle);
            wrenReleaseHandle(m_vm, handle);

            if (result != WREN_RESULT_SUCCESS) { return rc::Err(rc::ErrorCode::Internal); }
            return SlotToVariant(m_vm, 0);
        }

    private:
        [[nodiscard]] bool HasVariable(rc::StringView name) const
        {
            const rc::UTF8String mod = m_module;
            if (mod.IsEmpty() || !wrenHasModule(m_vm, CStr(mod))) { return false; }
            const rc::UTF8String nm = rc::ToUTF8(name);
            return wrenHasVariable(m_vm, CStr(mod), CStr(nm));
        }

        static void OnWrite(WrenVM*, const char* text) { WriteUtf8(&rc::ConsoleWrite, text); }
        static void OnError(WrenVM*, WrenErrorType, const char*, int, const char* message)
        {
            WriteUtf8(&rc::ConsoleWriteError, message);
        }

        WrenVM* m_vm = nullptr;
        rc::UTF8String m_module; // module of the last successful Load (globals/calls resolve here)
    };

    class WrenManager final : public IScriptManager
    {
    public:
        // Recorded for the upcoming foreign-class binding pass.
        void RegisterType(const rc::TypeInfo& type) override { m_types.PushBack(&type); }

        [[nodiscard]] rc::RefPtr<IScriptContext> CreateContext() override
        {
            return rc::RefPtr<IScriptContext>(rc::MakeRef<WrenContext>(rc::DefaultAllocator()));
        }

    private:
        rc::Array<const rc::TypeInfo*> m_types;
    };
}

export namespace raptor::script::wren
{
    // Creates a Wren-backed script manager.
    [[nodiscard]] rc::RefPtr<IScriptManager> CreateScriptManager()
    {
        return rc::RefPtr<IScriptManager>(rc::MakeRef<WrenManager>(rc::DefaultAllocator()));
    }
}
