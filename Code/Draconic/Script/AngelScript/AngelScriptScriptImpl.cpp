// Draconic::ScriptAngelScript - implementation (module IMPLEMENTATION unit).
//
// ALL AngelScript SDK contact lives here: the interface unit stays engine-types-only
// (GCC module hygiene - the SDK header never sits in an interface unit's global
// fragment). See AngelScriptScript.cppm for the emission mapping.
//
// Architecture:
//  - One asIScriptEngine per AngelScriptManager. RegisterType COLLECTS TypeInfos;
//    FinalizeTypes emits in TWO PHASES: RegisterObjectType for every collected type
//    first, then behaviours/properties/methods - declaration strings may therefore
//    reference any reflected type (the reason IScriptManager::FinalizeTypes exists).
//  - Every reflected instance visible to script is a BoxedVariant: a refcounted box
//    holding the engine Variant (value or Object), registered as asOBJ_REF with
//    generic factory/addref/release behaviours.
//  - An IScriptContext is a family of asIScriptModules on the shared engine (one
//    fresh module per Load; lookups target the most recent). Execution borrows
//    asIScriptContexts from the engine's context pool (nesting-safe).
//  - ScriptCallScope brackets EVERY dispatch into script (Build's global
//    initializers, Call, Invoke, factories) so native facades can resolve their
//    per-context services through CurrentScriptContext().

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

#include <angelscript.h>
#include <scriptstdstring/scriptstdstring.h>

#include <new>
#include <string>

module draconic.script.angelscript;

import draconic.core;
import draconic.script;

namespace core = draconic::core;

namespace draconic::script::angelscript
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

    inline bool IsValidIdentifier(const char* name) noexcept
    {
        if (name == nullptr || name[0] == '\0') { return false; }
        const auto alpha = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        };
        if (!alpha(name[0])) { return false; }
        for (const char* p = name + 1; *p != '\0'; ++p)
        {
            if (!alpha(*p) && !(*p >= '0' && *p <= '9')) { return false; }
        }
        return true;
    }

    inline core::StringView ViewOfAscii(const char* text) noexcept
    {
        return (text != nullptr)
            ? core::StringView(reinterpret_cast<const core::utf8char*>(text))
            : core::StringView{};
    }

    inline core::String StringFromStd(const std::string& s)
    {
        return core::String(core::StringView(
            reinterpret_cast<const core::utf8char*>(s.c_str()), s.size()));
    }

    inline std::string StdFromVariantString(const core::Variant& value)
    {
        if (const core::String* s = value.TryGet<core::String>())
        {
            return std::string(reinterpret_cast<const char*>(s->CStr()), s->Size());
        }
        return std::string();
    }

    // Numeric value of a Variant as double; `ok` false when it holds no number.
    inline double NumericOf(const core::Variant& value, bool& ok) noexcept
    {
        ok = true;
        if (const core::f64* d = value.TryGet<core::f64>()) { return *d; }
        if (const core::f32* f = value.TryGet<core::f32>()) { return static_cast<double>(*f); }
        if (const core::i32* i = value.TryGet<core::i32>()) { return static_cast<double>(*i); }
        if (const core::i64* i = value.TryGet<core::i64>()) { return static_cast<double>(*i); }
        if (const core::u32* u = value.TryGet<core::u32>()) { return static_cast<double>(*u); }
        if (const core::u64* u = value.TryGet<core::u64>()) { return static_cast<double>(*u); }
        if (const core::i16* i = value.TryGet<core::i16>()) { return static_cast<double>(*i); }
        if (const core::u16* u = value.TryGet<core::u16>()) { return static_cast<double>(*u); }
        if (const core::i8* i = value.TryGet<core::i8>()) { return static_cast<double>(*i); }
        if (const core::u8* u = value.TryGet<core::u8>()) { return static_cast<double>(*u); }
        if (const bool* b = value.TryGet<bool>()) { return *b ? 1.0 : 0.0; }
        ok = false;
        return 0.0;
    }

    // A number, coerced into a Variant of the reflected type a callee expects
    // (default f64, mirroring the Wren backend's marshalling currency).
    inline core::Variant CoerceNumber(double d, const core::TypeInfo* expected)
    {
        using namespace core;
        if (expected == &TypeOf<f32>()) { return Variant::From<f32>(static_cast<f32>(d)); }
        if (expected == &TypeOf<i32>()) { return Variant::From<i32>(static_cast<i32>(d)); }
        if (expected == &TypeOf<i64>()) { return Variant::From<i64>(static_cast<i64>(d)); }
        if (expected == &TypeOf<u32>()) { return Variant::From<u32>(static_cast<u32>(d)); }
        if (expected == &TypeOf<u64>()) { return Variant::From<u64>(static_cast<u64>(d)); }
        if (expected == &TypeOf<i16>()) { return Variant::From<i16>(static_cast<i16>(d)); }
        if (expected == &TypeOf<u16>()) { return Variant::From<u16>(static_cast<u16>(d)); }
        if (expected == &TypeOf<i8>())  { return Variant::From<i8>(static_cast<i8>(d)); }
        if (expected == &TypeOf<u8>())  { return Variant::From<u8>(static_cast<u8>(d)); }
        if (expected == &TypeOf<bool>()) { return Variant::From<bool>(d != 0.0); }
        return Variant::From<f64>(d);
    }

    // Reflected scalar/string -> AngelScript type name (null when `type` is not a
    // primitive - i.e. it needs an object-type registration instead).
    inline const char* PrimitiveDeclName(const core::TypeInfo* type) noexcept
    {
        using namespace core;
        if (type == &TypeOf<f32>()) { return "float"; }
        if (type == &TypeOf<f64>()) { return "double"; }
        if (type == &TypeOf<bool>()) { return "bool"; }
        if (type == &TypeOf<i32>()) { return "int"; }
        if (type == &TypeOf<i64>()) { return "int64"; }
        if (type == &TypeOf<u32>()) { return "uint"; }
        if (type == &TypeOf<u64>()) { return "uint64"; }
        if (type == &TypeOf<i16>()) { return "int16"; }
        if (type == &TypeOf<u16>()) { return "uint16"; }
        if (type == &TypeOf<i8>())  { return "int8"; }
        if (type == &TypeOf<u8>())  { return "uint8"; }
        if (type == &TypeOf<String>()) { return "string"; }
        return nullptr;
    }

    inline constexpr int kMaxArgs = 8;

    // Every reflected instance held by script: a refcounted box around a Variant.
    struct BoxedVariant
    {
        core::Variant value;
        core::i32 refCount = 1;
    };

    inline BoxedVariant* NewBox(core::Variant value)
    {
        BoxedVariant* box = core::DefaultAllocator().New<BoxedVariant>();
        box->value = core::Move(value);
        return box;
    }

    inline void ReleaseBox(BoxedVariant* box) noexcept
    {
        if (box != nullptr && --box->refCount == 0)
        {
            core::DefaultAllocator().Delete(box);
        }
    }

    class AngelScriptManager;

    // Auxiliary payload for every generic registration (AngelScript hands it back
    // via asIScriptGeneric::GetAuxiliary - no trampoline pool needed).
    struct Binding
    {
        enum class Kind { Constructor, PropertyGet, PropertySet, Method };
        Kind kind;
        AngelScriptManager* manager;
        const core::TypeInfo* type;
        const core::ConstructorInfo* constructor;
        const core::PropertyInfo* property;
        const core::MethodInfo* method;
    };

    void FactoryDispatch(asIScriptGeneric* gen);
    void AddRefDispatch(asIScriptGeneric* gen);
    void ReleaseDispatch(asIScriptGeneric* gen);
    void PropertyGetDispatch(asIScriptGeneric* gen);
    void PropertySetDispatch(asIScriptGeneric* gen);
    void MethodDispatch(asIScriptGeneric* gen);

    class AngelScriptManager final : public IScriptManager
    {
    public:
        AngelScriptManager()
        {
            m_engine = asCreateScriptEngine();
            m_engine->SetMessageCallback(asFUNCTION(&AngelScriptManager::OnMessage), this,
                                         asCALL_CDECL);
            RegisterStdString(m_engine);
            m_stringTypeId = m_engine->GetTypeIdByDecl("string");
        }

        ~AngelScriptManager() override
        {
            if (m_engine != nullptr) { m_engine->ShutDownAndRelease(); }
            for (Binding* binding : m_bindings)
            {
                core::DefaultAllocator().Delete(binding);
            }
        }

        AngelScriptManager(const AngelScriptManager&) = delete;
        AngelScriptManager& operator=(const AngelScriptManager&) = delete;

        // ---- IScriptManager --------------------------------------------------
        void RegisterType(const core::TypeInfo& type) override
        {
            for (const core::TypeInfo* existing : m_types)
            {
                if (existing == &type) { return; }
            }
            m_types.PushBack(&type);
            if (m_finalized)
            {
                // Late registration after finalize: run both phases for this type
                // alone (everything else is already declared).
                DeclareType(type);
                BindType(type);
            }
        }

        void FinalizeTypes() override
        {
            if (m_finalized) { return; }
            m_finalized = true;
            // Phase 1: DECLARE every collected type - after this, any declaration
            // string may reference any reflected type.
            for (const core::TypeInfo* type : m_types) { DeclareType(*type); }
            // Phase 2: bind members (factories, properties, methods, statics).
            for (const core::TypeInfo* type : m_types) { BindType(*type); }
        }

        [[nodiscard]] core::RefPtr<IScriptContext> CreateContext() override;

        void CollectGarbage() override
        {
            // AngelScript's GC is incremental; one full pass keeps the battery's
            // "callable any time" promise while staying frame-budget friendly.
            if (m_engine != nullptr) { m_engine->GarbageCollect(asGC_FULL_CYCLE); }
        }

        // ---- shared services for contexts / dispatchers ---------------------
        [[nodiscard]] asIScriptEngine* Engine() const noexcept { return m_engine; }
        [[nodiscard]] int StringTypeId() const noexcept { return m_stringTypeId; }

        // The reflected type behind an AngelScript type id (handle flags ignored);
        // null when the id is not one of OUR registered object types.
        [[nodiscard]] const core::TypeInfo* TypeInfoForTypeId(int typeId) const noexcept
        {
            const int base = typeId & ~(asTYPEID_OBJHANDLE | asTYPEID_HANDLETOCONST);
            for (const RegisteredType& entry : m_registered)
            {
                if (entry.typeId == base) { return entry.type; }
            }
            return nullptr;
        }

        // Script argument -> engine Variant (generic calling convention).
        [[nodiscard]] core::Variant ValueFromArg(asIScriptGeneric* gen, asUINT index,
                                                 const core::TypeInfo* expected) const
        {
            const int typeId = gen->GetArgTypeId(index);
            switch (typeId)
            {
                case asTYPEID_BOOL:
                {
                    const bool b = gen->GetArgByte(index) != 0;
                    return (expected == nullptr || expected == &core::TypeOf<bool>())
                        ? core::Variant::From<bool>(b)
                        : CoerceNumber(b ? 1.0 : 0.0, expected);
                }
                case asTYPEID_INT8:   return CoerceNumber(static_cast<core::i8>(gen->GetArgByte(index)), expected);
                case asTYPEID_UINT8:  return CoerceNumber(gen->GetArgByte(index), expected);
                case asTYPEID_INT16:  return CoerceNumber(static_cast<core::i16>(gen->GetArgWord(index)), expected);
                case asTYPEID_UINT16: return CoerceNumber(gen->GetArgWord(index), expected);
                case asTYPEID_INT32:  return CoerceNumber(static_cast<core::i32>(gen->GetArgDWord(index)), expected);
                case asTYPEID_UINT32: return CoerceNumber(gen->GetArgDWord(index), expected);
                case asTYPEID_INT64:  return CoerceNumber(static_cast<double>(static_cast<core::i64>(gen->GetArgQWord(index))), expected);
                case asTYPEID_UINT64: return CoerceNumber(static_cast<double>(gen->GetArgQWord(index)), expected);
                case asTYPEID_FLOAT:  return CoerceNumber(gen->GetArgFloat(index), expected);
                case asTYPEID_DOUBLE: return CoerceNumber(gen->GetArgDouble(index), expected);
                default: break;
            }
            if (typeId == m_stringTypeId)
            {
                const std::string* s = static_cast<const std::string*>(gen->GetArgAddress(index));
                return (s != nullptr)
                    ? core::Variant::From<core::String>(StringFromStd(*s))
                    : core::Variant{};
            }
            if ((typeId & asTYPEID_OBJHANDLE) != 0 && TypeInfoForTypeId(typeId) != nullptr)
            {
                const BoxedVariant* box = static_cast<const BoxedVariant*>(gen->GetArgObject(index));
                return (box != nullptr) ? box->value : core::Variant{};
            }
            return core::Variant{};
        }

        // Engine Variant -> the generic call's declared return slot. An empty
        // Variant produces the type's zero value (null handle / empty string).
        void SetGenericReturn(asIScriptGeneric* gen, const core::Variant& value) const
        {
            const int typeId = gen->GetReturnTypeId();
            if (typeId == asTYPEID_VOID) { return; }
            bool ok = false;
            const double number = NumericOf(value, ok);
            switch (typeId)
            {
                case asTYPEID_BOOL:   gen->SetReturnByte(number != 0.0 ? 1 : 0); return;
                case asTYPEID_INT8:
                case asTYPEID_UINT8:  gen->SetReturnByte(static_cast<asBYTE>(static_cast<core::i64>(number))); return;
                case asTYPEID_INT16:
                case asTYPEID_UINT16: gen->SetReturnWord(static_cast<asWORD>(static_cast<core::i64>(number))); return;
                case asTYPEID_INT32:
                case asTYPEID_UINT32: gen->SetReturnDWord(static_cast<asDWORD>(static_cast<core::i64>(number))); return;
                case asTYPEID_INT64:
                case asTYPEID_UINT64: gen->SetReturnQWord(static_cast<asQWORD>(static_cast<core::i64>(number))); return;
                case asTYPEID_FLOAT:  gen->SetReturnFloat(static_cast<float>(number)); return;
                case asTYPEID_DOUBLE: gen->SetReturnDouble(number); return;
                default: break;
            }
            if (typeId == m_stringTypeId)
            {
                new (gen->GetAddressOfReturnLocation()) std::string(StdFromVariantString(value));
                return;
            }
            if ((typeId & asTYPEID_OBJHANDLE) != 0)
            {
                BoxedVariant* box = (!value.IsEmpty() && TypeInfoForTypeId(typeId) != nullptr)
                    ? NewBox(value)
                    : nullptr;
                *static_cast<void**>(gen->GetAddressOfReturnLocation()) = box;
            }
        }

        // Typed script storage (module global / finished call's return register)
        // -> engine Variant. Numbers surface uniformly as f64, strings as String,
        // handles to OUR types as a copy of the boxed Variant (Wren parity).
        [[nodiscard]] core::Variant VariantFromTypedAddress(int typeId, void* address) const
        {
            if (address == nullptr) { return core::Variant{}; }
            switch (typeId)
            {
                case asTYPEID_BOOL:   return core::Variant::From<bool>(*static_cast<bool*>(address));
                case asTYPEID_INT8:   return core::Variant::From<core::f64>(*static_cast<core::i8*>(address));
                case asTYPEID_UINT8:  return core::Variant::From<core::f64>(*static_cast<core::u8*>(address));
                case asTYPEID_INT16:  return core::Variant::From<core::f64>(*static_cast<core::i16*>(address));
                case asTYPEID_UINT16: return core::Variant::From<core::f64>(*static_cast<core::u16*>(address));
                case asTYPEID_INT32:  return core::Variant::From<core::f64>(*static_cast<core::i32*>(address));
                case asTYPEID_UINT32: return core::Variant::From<core::f64>(*static_cast<core::u32*>(address));
                case asTYPEID_INT64:  return core::Variant::From<core::f64>(static_cast<core::f64>(*static_cast<core::i64*>(address)));
                case asTYPEID_UINT64: return core::Variant::From<core::f64>(static_cast<core::f64>(*static_cast<core::u64*>(address)));
                case asTYPEID_FLOAT:  return core::Variant::From<core::f64>(*static_cast<float*>(address));
                case asTYPEID_DOUBLE: return core::Variant::From<core::f64>(*static_cast<double*>(address));
                default: break;
            }
            if (typeId == m_stringTypeId)
            {
                return core::Variant::From<core::String>(
                    StringFromStd(*static_cast<const std::string*>(address)));
            }
            if ((typeId & asTYPEID_OBJHANDLE) != 0 && TypeInfoForTypeId(typeId) != nullptr)
            {
                const BoxedVariant* box = *static_cast<const BoxedVariant* const*>(address);
                return (box != nullptr) ? box->value : core::Variant{};
            }
            return core::Variant{};
        }

        // Engine Variant -> typed script storage (SetGlobal). False when the
        // slot's type cannot take the value.
        bool WriteTypedAddress(int typeId, void* address, const core::Variant& value) const
        {
            if (address == nullptr) { return false; }
            bool ok = false;
            const double number = NumericOf(value, ok);
            switch (typeId)
            {
                case asTYPEID_BOOL:   if (ok) { *static_cast<bool*>(address) = number != 0.0; } return ok;
                case asTYPEID_INT8:   if (ok) { *static_cast<core::i8*>(address) = static_cast<core::i8>(number); } return ok;
                case asTYPEID_UINT8:  if (ok) { *static_cast<core::u8*>(address) = static_cast<core::u8>(number); } return ok;
                case asTYPEID_INT16:  if (ok) { *static_cast<core::i16*>(address) = static_cast<core::i16>(number); } return ok;
                case asTYPEID_UINT16: if (ok) { *static_cast<core::u16*>(address) = static_cast<core::u16>(number); } return ok;
                case asTYPEID_INT32:  if (ok) { *static_cast<core::i32*>(address) = static_cast<core::i32>(number); } return ok;
                case asTYPEID_UINT32: if (ok) { *static_cast<core::u32*>(address) = static_cast<core::u32>(number); } return ok;
                case asTYPEID_INT64:  if (ok) { *static_cast<core::i64*>(address) = static_cast<core::i64>(number); } return ok;
                case asTYPEID_UINT64: if (ok) { *static_cast<core::u64*>(address) = static_cast<core::u64>(number); } return ok;
                case asTYPEID_FLOAT:  if (ok) { *static_cast<float*>(address) = static_cast<float>(number); } return ok;
                case asTYPEID_DOUBLE: if (ok) { *static_cast<double*>(address) = number; } return ok;
                default: break;
            }
            if (typeId == m_stringTypeId)
            {
                if (value.TryGet<core::String>() == nullptr) { return false; }
                *static_cast<std::string*>(address) = StdFromVariantString(value);
                return true;
            }
            if ((typeId & asTYPEID_OBJHANDLE) != 0 && TypeInfoForTypeId(typeId) != nullptr)
            {
                BoxedVariant** slot = static_cast<BoxedVariant**>(address);
                ReleaseBox(*slot);
                *slot = value.IsEmpty() ? nullptr : NewBox(value);
                return true;
            }
            return false;
        }

        // ---- compile-message routing ----------------------------------------
        // Build both compiles AND runs global initializers, and errors from either
        // phase arrive through the one engine message callback. Buffer during
        // Build; the caller classifies by Build's return code (compile failure vs
        // asINIT_GLOBAL_VARS_FAILED) and flushes with the right ScriptErrorKind.
        void BeginMessageCapture()
        {
            m_capturedMessages.Clear();
            m_capturing = true;
        }

        void EndMessageCapture(IScriptErrorHandler* handler, ScriptErrorKind kind)
        {
            m_capturing = false;
            for (const CapturedMessage& message : m_capturedMessages)
            {
                if (handler != nullptr)
                {
                    const ScriptError error{ kind, core::StringView(message.section),
                                             message.row, core::StringView(message.text) };
                    handler->OnError(error);
                }
                else
                {
                    core::ConsoleWriteError(core::StringView(message.text));
                }
            }
            m_capturedMessages.Clear();
        }

        [[nodiscard]] Binding* MakeBinding(Binding binding)
        {
            Binding* stored = core::DefaultAllocator().New<Binding>(binding);
            m_bindings.PushBack(stored);
            return stored;
        }

    private:
        struct RegisteredType
        {
            const core::TypeInfo* type;
            int typeId;
        };

        struct CapturedMessage
        {
            core::String section;
            core::i32 row;
            core::String text;
        };

        static void OnMessage(const asSMessageInfo* message, void* param)
        {
            AngelScriptManager* self = static_cast<AngelScriptManager*>(param);
            if (message->type != asMSGTYPE_ERROR) { return; } // warnings/info: quiet
            if (self->m_capturing)
            {
                CapturedMessage captured;
                captured.section = core::String(ViewOfAscii(message->section));
                captured.row = static_cast<core::i32>(message->row);
                captured.text = core::String(ViewOfAscii(message->message));
                self->m_capturedMessages.PushBack(core::Move(captured));
                return;
            }
            core::ConsoleWriteError(ViewOfAscii(message->message));
        }

        [[nodiscard]] bool IsDeclared(const core::TypeInfo* type) const noexcept
        {
            for (const RegisteredType& entry : m_registered)
            {
                if (entry.type == type) { return true; }
            }
            return false;
        }

        // Appends the AngelScript declaration name for a reflected type: scalars/
        // string by value (strings as `const string &in` in parameter position),
        // declared object types as handles. False = not expressible, skip member.
        bool AppendDeclType(core::String& out, const core::TypeInfo* type, bool isParam) const
        {
            if (type == nullptr) { return false; }
            if (const char* primitive = PrimitiveDeclName(type))
            {
                if (isParam && type == &core::TypeOf<core::String>())
                {
                    AppendAscii(out, "const string &in");
                    return true;
                }
                AppendAscii(out, primitive);
                return true;
            }
            if (!IsDeclared(type)) { return false; }
            AppendAscii(out, type->name);
            AppendAscii(out, "@");
            return true;
        }

        // Phase 1: declare the object type (skips scalars/string/enums/containers
        // and anything AngelScript's own registry rejects, e.g. name collisions).
        void DeclareType(const core::TypeInfo& type)
        {
            if (!IsValidIdentifier(type.name)) { return; }
            if (PrimitiveDeclName(&type) != nullptr) { return; } // scalar/string currency
            if (type.enumeratorCount > 0 || type.container != nullptr) { return; }
            if (IsDeclared(&type)) { return; }
            const int typeId = m_engine->RegisterObjectType(type.name, 0, asOBJ_REF);
            if (typeId < 0)
            {
                DRACONIC_LOG_DEBUG(u8"Script",
                    u8"AngelScript: could not declare reflected type '{}' ({})",
                    ViewOfAscii(type.name), typeId);
                return;
            }
            m_registered.PushBack(RegisteredType{ &type, typeId });
        }

        // Phase 2: bind the declared type's members.
        void BindType(const core::TypeInfo& type)
        {
            if (!IsDeclared(&type)) { return; }
            const char* name = type.name;

            (void)m_engine->RegisterObjectBehaviour(name, asBEHAVE_ADDREF, "void f()",
                asFUNCTION(AddRefDispatch), asCALL_GENERIC);
            (void)m_engine->RegisterObjectBehaviour(name, asBEHAVE_RELEASE, "void f()",
                asFUNCTION(ReleaseDispatch), asCALL_GENERIC);

            core::Array<core::String> used; // exact-declaration dedupe

            // Factories: one per reflected constructor (`builder.Constructor()` is
            // the contract's constructibility requirement).
            for (core::usize i = 0; i < core::ConstructorCount(type); ++i)
            {
                const core::ConstructorInfo& constructor = core::ConstructorAt(type, i);
                core::String decl;
                AppendAscii(decl, name);
                AppendAscii(decl, "@ f(");
                if (!AppendParams(decl, constructor.params, constructor.paramCount)) { continue; }
                AppendAscii(decl, ")");
                if (IsUsed(used, decl)) { continue; }
                Binding* binding = MakeBinding(Binding{ Binding::Kind::Constructor, this,
                                                        &type, &constructor, nullptr, nullptr });
                if (m_engine->RegisterObjectBehaviour(name, asBEHAVE_FACTORY, CStr(decl),
                        asFUNCTION(FactoryDispatch), asCALL_GENERIC, binding) >= 0)
                {
                    used.PushBack(core::Move(decl));
                }
            }

            // Properties -> virtual property accessors (`v.x`, `v.x = 9`).
            for (core::usize i = 0; i < core::PropertyCount(type); ++i)
            {
                const core::PropertyInfo& property = core::PropertyAt(type, i);
                if (!IsValidIdentifier(property.name)) { continue; }
                {
                    core::String decl;
                    if (!AppendDeclType(decl, property.type, /*isParam*/ false)) { continue; }
                    AppendAscii(decl, " get_");
                    AppendAscii(decl, property.name);
                    AppendAscii(decl, "() property");
                    Binding* binding = MakeBinding(Binding{ Binding::Kind::PropertyGet, this,
                                                            &type, nullptr, &property, nullptr });
                    (void)m_engine->RegisterObjectMethod(name, CStr(decl),
                        asFUNCTION(PropertyGetDispatch), asCALL_GENERIC, binding);
                }
                const bool readOnly = (static_cast<core::u32>(property.flags)
                    & static_cast<core::u32>(core::PropertyFlags::ReadOnly)) != 0;
                if (!readOnly)
                {
                    core::String decl;
                    AppendAscii(decl, "void set_");
                    AppendAscii(decl, property.name);
                    AppendAscii(decl, "(");
                    if (!AppendDeclType(decl, property.type, /*isParam*/ true)) { continue; }
                    AppendAscii(decl, ") property");
                    Binding* binding = MakeBinding(Binding{ Binding::Kind::PropertySet, this,
                                                            &type, nullptr, &property, nullptr });
                    (void)m_engine->RegisterObjectMethod(name, CStr(decl),
                        asFUNCTION(PropertySetDispatch), asCALL_GENERIC, binding);
                }
            }

            // Methods. AngelScript overloads by full signature, so EVERY reflected
            // overload registers distinctly (no Wren-style arity collapsing).
            // Statics become global functions in a namespace named after the class
            // - script calls read `Float3::Dot(a, b)`.
            core::Array<core::String> usedStatics;
            for (core::usize i = 0; i < core::MethodCount(type); ++i)
            {
                const core::MethodInfo& method = core::MethodAt(type, i);
                if (!IsValidIdentifier(method.name)) { continue; }
                const core::TypeInfo* returnType =
                    (method.returnType != nullptr) ? method.returnType() : nullptr;
                core::String decl;
                if (returnType == nullptr) { AppendAscii(decl, "void"); }
                else if (!AppendDeclType(decl, returnType, /*isParam*/ false)) { continue; }
                AppendAscii(decl, " ");
                AppendAscii(decl, method.name);
                AppendAscii(decl, "(");
                if (!AppendParams(decl, method.params, method.paramCount)) { continue; }
                AppendAscii(decl, ")");

                core::Array<core::String>& dedupe = method.isStatic ? usedStatics : used;
                if (IsUsed(dedupe, decl)) { continue; }
                Binding* binding = MakeBinding(Binding{ Binding::Kind::Method, this,
                                                        &type, nullptr, nullptr, &method });
                int r;
                if (method.isStatic)
                {
                    (void)m_engine->SetDefaultNamespace(name);
                    r = m_engine->RegisterGlobalFunction(CStr(decl),
                        asFUNCTION(MethodDispatch), asCALL_GENERIC, binding);
                    (void)m_engine->SetDefaultNamespace("");
                }
                else
                {
                    r = m_engine->RegisterObjectMethod(name, CStr(decl),
                        asFUNCTION(MethodDispatch), asCALL_GENERIC, binding);
                }
                if (r >= 0) { dedupe.PushBack(core::Move(decl)); }
            }
        }

        bool AppendParams(core::String& decl, const core::ParamInfo* params, core::u32 count) const
        {
            for (core::u32 p = 0; p < count; ++p)
            {
                if (!AppendDeclType(decl, params[p].type != nullptr ? params[p].type() : nullptr,
                                    /*isParam*/ true))
                {
                    return false;
                }
                if (p + 1 < count) { AppendAscii(decl, ", "); }
            }
            return true;
        }

        [[nodiscard]] static bool IsUsed(const core::Array<core::String>& used,
                                         const core::String& decl) noexcept
        {
            for (const core::String& existing : used)
            {
                if (existing == decl) { return true; }
            }
            return false;
        }

        asIScriptEngine* m_engine = nullptr;
        int m_stringTypeId = -1;
        bool m_finalized = false;
        bool m_capturing = false;
        core::u32 m_nextContextId = 0;
        core::Array<const core::TypeInfo*> m_types;
        core::Array<RegisteredType> m_registered;
        core::Array<Binding*> m_bindings;
        core::Array<CapturedMessage> m_capturedMessages;
    };

    // ---- generic dispatchers (run DURING script execution; the surrounding
    // Call/Load/Invoke already pushed the ScriptCallScope) ---------------------

    // Generic-call handle ownership (asEP_GENERIC_CALL_MODE == 1, the modern
    // default): a plain `Type@` parameter arrives with a reference the CALLEE
    // owns and must release - the engine adds no cleanup instruction for it.
    // Every dispatcher that takes arguments calls this after marshalling (the
    // Variants hold copies by then). Missing this leaks one reference per
    // object argument (found by ASAN).
    inline void ReleaseHandleArgs(asIScriptGeneric* gen, const AngelScriptManager* manager)
    {
        const asUINT argc = gen->GetArgCount();
        for (asUINT i = 0; i < argc; ++i)
        {
            const int typeId = gen->GetArgTypeId(i);
            if ((typeId & asTYPEID_OBJHANDLE) != 0 && manager->TypeInfoForTypeId(typeId) != nullptr)
            {
                ReleaseBox(static_cast<BoxedVariant*>(gen->GetArgObject(i)));
            }
        }
    }
    void FactoryDispatch(asIScriptGeneric* gen)
    {
        const Binding* binding = static_cast<const Binding*>(gen->GetAuxiliary());
        int argc = static_cast<int>(gen->GetArgCount());
        if (argc > kMaxArgs) { argc = kMaxArgs; }
        core::Variant args[kMaxArgs];
        for (int i = 0; i < argc; ++i)
        {
            const core::ParamInfo& param = binding->constructor->params[i];
            args[i] = binding->manager->ValueFromArg(gen, static_cast<asUINT>(i),
                                                     param.type != nullptr ? param.type() : nullptr);
        }
        ReleaseHandleArgs(gen, binding->manager);
        core::Result<core::Variant> created = binding->constructor->invoke(
            core::Span<core::Variant>{ args, static_cast<core::usize>(argc) });
        if (!created.HasValue())
        {
            *static_cast<void**>(gen->GetAddressOfReturnLocation()) = nullptr;
            if (asIScriptContext* active = asGetActiveContext())
            {
                active->SetException("reflected constructor failed");
            }
            return;
        }
        *static_cast<void**>(gen->GetAddressOfReturnLocation()) =
            NewBox(core::Move(created.Value()));
    }

    void AddRefDispatch(asIScriptGeneric* gen)
    {
        ++static_cast<BoxedVariant*>(gen->GetObject())->refCount;
    }

    void ReleaseDispatch(asIScriptGeneric* gen)
    {
        ReleaseBox(static_cast<BoxedVariant*>(gen->GetObject()));
    }

    void PropertyGetDispatch(asIScriptGeneric* gen)
    {
        const Binding* binding = static_cast<const Binding*>(gen->GetAuxiliary());
        BoxedVariant* self = static_cast<BoxedVariant*>(gen->GetObject());
        core::Instance instance = core::ToInstance(self->value);
        binding->manager->SetGenericReturn(gen, core::GetProperty(*binding->property, instance));
    }

    void PropertySetDispatch(asIScriptGeneric* gen)
    {
        const Binding* binding = static_cast<const Binding*>(gen->GetAuxiliary());
        BoxedVariant* self = static_cast<BoxedVariant*>(gen->GetObject());
        core::Instance instance = core::ToInstance(self->value);
        const core::Variant value = binding->manager->ValueFromArg(gen, 0, binding->property->type);
        ReleaseHandleArgs(gen, binding->manager);
        (void)core::SetProperty(*binding->property, instance, value);
    }

    void MethodDispatch(asIScriptGeneric* gen)
    {
        const Binding* binding = static_cast<const Binding*>(gen->GetAuxiliary());
        const core::MethodInfo& method = *binding->method;
        int argc = static_cast<int>(gen->GetArgCount());
        if (argc > kMaxArgs) { argc = kMaxArgs; }
        core::Variant args[kMaxArgs];
        for (int i = 0; i < argc; ++i)
        {
            const core::TypeInfo* expected = (i < static_cast<int>(method.paramCount)
                && method.params[i].type != nullptr) ? method.params[i].type() : nullptr;
            args[i] = binding->manager->ValueFromArg(gen, static_cast<asUINT>(i), expected);
        }
        ReleaseHandleArgs(gen, binding->manager);
        const core::Span<core::Variant> argSpan{ args, static_cast<core::usize>(argc) };
        core::Result<core::Variant> result = core::Err(core::ErrorCode::Internal);
        if (method.isStatic)
        {
            result = core::InvokeStatic(method, argSpan);
        }
        else
        {
            BoxedVariant* self = static_cast<BoxedVariant*>(gen->GetObject());
            result = core::InvokeMethod(method, core::ToInstance(self->value), argSpan);
        }
        binding->manager->SetGenericReturn(gen,
            result.HasValue() ? result.Value() : core::Variant{});
    }

    // ---- context -------------------------------------------------------------
    class AngelScriptContext final : public IScriptContext
    {
    public:
        AngelScriptContext(core::RefPtr<AngelScriptManager> manager, core::u32 id)
            : m_manager(core::Move(manager))
        {
            AppendAscii(m_namePrefix, "ctx");
            AppendUint(m_namePrefix, id);
        }

        ~AngelScriptContext() override
        {
            // No ScriptObject outlives its context (they hold a strong ref), so
            // discarding this context's modules is safe here.
            for (asIScriptModule* module : m_ownedModules) { module->Discard(); }
        }

        AngelScriptContext(const AngelScriptContext&) = delete;
        AngelScriptContext& operator=(const AngelScriptContext&) = delete;

        void SetErrorHandler(IScriptErrorHandler* handler) override { m_errorHandler = handler; }

        core::Status Load(core::StringView source, core::StringView chunkName) override
        {
            asIScriptEngine* engine = m_manager->Engine();
            core::String moduleName = m_namePrefix;
            AppendAscii(moduleName, ":");
            AppendUint(moduleName, m_loadCounter++);
            asIScriptModule* module = engine->GetModule(CStr(moduleName), asGM_ALWAYS_CREATE);
            if (module == nullptr) { return core::Status{ core::ErrorCode::Internal }; }

            const core::String section(chunkName);
            (void)module->AddScriptSection(CStr(section),
                reinterpret_cast<const char*>(source.Data()), source.Size());

            // Build compiles AND runs global initializers; classify buffered errors
            // by the return code (compile failure vs failed global init).
            int result;
            m_manager->BeginMessageCapture();
            {
                ScriptCallScope scope(this);
                result = module->Build();
            }
            const bool initFailed = (result == asINIT_GLOBAL_VARS_FAILED);
            m_manager->EndMessageCapture(m_errorHandler,
                initFailed ? ScriptErrorKind::Runtime : ScriptErrorKind::Compile);
            if (result < 0)
            {
                module->Discard();
                return core::Status{ initFailed ? core::ErrorCode::Internal
                                                : core::ErrorCode::InvalidArgument };
            }

            m_ownedModules.PushBack(module);
            m_module = module;

            // Top-level entry convention: a module-level `void main()` runs at load
            // (AngelScript has no top-level statements; this is the runtime-fault
            // and script-setup seam the contract's Load semantics map onto).
            if (asIScriptFunction* entry = module->GetFunctionByName("main"))
            {
                core::Result<core::Variant> ran = ExecuteCall(entry, nullptr,
                                                              core::Span<core::Variant>{});
                if (!ran.HasValue()) { return core::Status{ core::ErrorCode::Internal }; }
            }
            return core::Status{};
        }

        void SetGlobal(core::StringView name, const core::Variant& value) override
        {
            if (m_module == nullptr) { return; }
            const core::String globalName(name);
            const int index = m_module->GetGlobalVarIndexByName(CStr(globalName));
            if (index < 0) { return; }
            int typeId = 0;
            (void)m_module->GetGlobalVar(static_cast<asUINT>(index), nullptr, nullptr, &typeId,
                                         nullptr);
            (void)m_manager->WriteTypedAddress(typeId,
                m_module->GetAddressOfGlobalVar(static_cast<asUINT>(index)), value);
        }

        [[nodiscard]] core::Variant GetGlobal(core::StringView name) override
        {
            if (m_module == nullptr) { return core::Variant{}; }
            const core::String globalName(name);
            const int index = m_module->GetGlobalVarIndexByName(CStr(globalName));
            if (index < 0) { return core::Variant{}; }
            int typeId = 0;
            (void)m_module->GetGlobalVar(static_cast<asUINT>(index), nullptr, nullptr, &typeId,
                                         nullptr);
            return m_manager->VariantFromTypedAddress(typeId,
                m_module->GetAddressOfGlobalVar(static_cast<asUINT>(index)));
        }

        [[nodiscard]] bool HasFunction(core::StringView name) const override
        {
            return FindFunction(name, -1) != nullptr;
        }

        [[nodiscard]] core::Result<core::Variant> Call(core::StringView function,
                                                       core::Span<core::Variant> args) override
        {
            // Strict arity: never Execute with unset argument slots.
            asIScriptFunction* target = FindFunction(function, static_cast<int>(args.Size()));
            if (target == nullptr || target->GetParamCount() != args.Size())
            {
                return core::Err(core::ErrorCode::NotFound);
            }
            return ExecuteCall(target, nullptr, args);
        }

        [[nodiscard]] core::RefPtr<ScriptObject> CreateInstance(
            core::StringView className, core::Span<core::Variant> args) override;

        // Prepare + execute a script function on the engine's pooled contexts.
        // The public seam AngelScriptObject::Invoke dispatches through as well.
        [[nodiscard]] core::Result<core::Variant> ExecuteCall(
            asIScriptFunction* function, void* object, core::Span<core::Variant> args)
        {
            asIScriptEngine* engine = m_manager->Engine();
            asIScriptContext* executor = engine->RequestContext();
            if (executor == nullptr || executor->Prepare(function) < 0)
            {
                if (executor != nullptr) { engine->ReturnContext(executor); }
                return core::Err(core::ErrorCode::Internal);
            }
            if (object != nullptr) { (void)executor->SetObject(object); }

            std::string stringTemps[kMaxArgs];
            BoxedVariant* boxTemps[kMaxArgs] = {};
            BindArgs(executor, function, args, stringTemps, boxTemps);

            int result;
            {
                ScriptCallScope scope(this);
                result = executor->Execute();
            }
            for (BoxedVariant* box : boxTemps) { ReleaseBox(box); }

            core::Result<core::Variant> outcome = core::Err(core::ErrorCode::Internal);
            if (result == asEXECUTION_FINISHED)
            {
                const int returnTypeId = function->GetReturnTypeId();
                outcome = (returnTypeId == asTYPEID_VOID)
                    ? core::Result<core::Variant>(core::Variant{})
                    : core::Result<core::Variant>(m_manager->VariantFromTypedAddress(
                          returnTypeId, executor->GetAddressOfReturnValue()));
            }
            else if (result == asEXECUTION_EXCEPTION)
            {
                ReportException(executor);
            }
            engine->ReturnContext(executor);
            return outcome;
        }

        [[nodiscard]] AngelScriptManager& Manager() noexcept { return *m_manager; }

    private:
        [[nodiscard]] asIScriptFunction* FindFunction(core::StringView name, int argc) const
        {
            if (m_module == nullptr) { return nullptr; }
            const core::String functionName(name);
            asIScriptFunction* byName = nullptr;
            for (asUINT i = 0; i < m_module->GetFunctionCount(); ++i)
            {
                asIScriptFunction* candidate = m_module->GetFunctionByIndex(i);
                const char* candidateName = candidate->GetName();
                if (candidateName == nullptr || !NameEq(candidateName, CStr(functionName)))
                {
                    continue;
                }
                if (argc < 0 || static_cast<int>(candidate->GetParamCount()) == argc)
                {
                    return candidate;
                }
                if (byName == nullptr) { byName = candidate; }
            }
            return byName;
        }

        void BindArgs(asIScriptContext* executor, asIScriptFunction* function,
                      core::Span<core::Variant> args, std::string* stringTemps,
                      BoxedVariant** boxTemps)
        {
            const core::usize limit = function->GetParamCount();
            for (core::usize i = 0;
                 i < args.Size() && i < limit && i < static_cast<core::usize>(kMaxArgs); ++i)
            {
                const asUINT arg = static_cast<asUINT>(i);
                int typeId = 0;
                (void)function->GetParam(arg, &typeId);
                const core::Variant& value = args[i];
                bool ok = false;
                const double number = NumericOf(value, ok);
                switch (typeId)
                {
                    case asTYPEID_BOOL:   (void)executor->SetArgByte(arg, number != 0.0 ? 1 : 0); continue;
                    case asTYPEID_INT8:
                    case asTYPEID_UINT8:  (void)executor->SetArgByte(arg, static_cast<asBYTE>(static_cast<core::i64>(number))); continue;
                    case asTYPEID_INT16:
                    case asTYPEID_UINT16: (void)executor->SetArgWord(arg, static_cast<asWORD>(static_cast<core::i64>(number))); continue;
                    case asTYPEID_INT32:
                    case asTYPEID_UINT32: (void)executor->SetArgDWord(arg, static_cast<asDWORD>(static_cast<core::i64>(number))); continue;
                    case asTYPEID_INT64:
                    case asTYPEID_UINT64: (void)executor->SetArgQWord(arg, static_cast<asQWORD>(static_cast<core::i64>(number))); continue;
                    case asTYPEID_FLOAT:  (void)executor->SetArgFloat(arg, static_cast<float>(number)); continue;
                    case asTYPEID_DOUBLE: (void)executor->SetArgDouble(arg, number); continue;
                    default: break;
                }
                if (typeId == m_manager->StringTypeId())
                {
                    stringTemps[i] = StdFromVariantString(value);
                    // Copied for by-value params; referenced (temps outlive Execute)
                    // for &in params.
                    (void)executor->SetArgObject(arg, &stringTemps[i]);
                    continue;
                }
                if ((typeId & asTYPEID_OBJHANDLE) != 0
                    && m_manager->TypeInfoForTypeId(typeId) != nullptr && !value.IsEmpty())
                {
                    boxTemps[i] = NewBox(value); // SetArgObject AddRefs; ours released after Execute
                    (void)executor->SetArgObject(arg, boxTemps[i]);
                    continue;
                }
            }
        }

        void ReportException(asIScriptContext* executor)
        {
            const char* section = nullptr;
            const int line = executor->GetExceptionLineNumber(nullptr, &section);
            if (m_errorHandler != nullptr)
            {
                const ScriptError error{ ScriptErrorKind::Runtime, ViewOfAscii(section),
                                         static_cast<core::i32>(line),
                                         ViewOfAscii(executor->GetExceptionString()) };
                m_errorHandler->OnError(error);
                return;
            }
            core::ConsoleWriteError(ViewOfAscii(executor->GetExceptionString()));
        }

        core::RefPtr<AngelScriptManager> m_manager;
        IScriptErrorHandler* m_errorHandler = nullptr;
        core::String m_namePrefix;
        core::u32 m_loadCounter = 0;
        asIScriptModule* m_module = nullptr;            // most recent successful Load
        core::Array<asIScriptModule*> m_ownedModules;
    };

    // A live instance of a script-declared class. Holds the asIScriptObject plus a
    // strong reference to its owning context (which keeps the manager - and thus
    // the engine - alive).
    class AngelScriptObject final : public ScriptObject
    {
    public:
        AngelScriptObject(core::RefPtr<AngelScriptContext> owner, asIScriptObject* instance) noexcept
            : m_owner(core::Move(owner)), m_instance(instance)
        {
        }

        ~AngelScriptObject() override
        {
            if (m_instance != nullptr) { m_instance->Release(); }
        }

        AngelScriptObject(const AngelScriptObject&) = delete;
        AngelScriptObject& operator=(const AngelScriptObject&) = delete;

        [[nodiscard]] core::Result<core::Variant> Invoke(core::StringView method,
                                                         core::Span<core::Variant> args) override
        {
            asITypeInfo* type = m_instance->GetObjectType();
            if (type == nullptr) { return core::Err(core::ErrorCode::NotFound); }
            const core::String methodName(method);
            asIScriptFunction* target = nullptr;
            for (asUINT i = 0; i < type->GetMethodCount(); ++i)
            {
                asIScriptFunction* candidate = type->GetMethodByIndex(i);
                const char* candidateName = candidate->GetName();
                if (candidateName != nullptr && NameEq(candidateName, CStr(methodName))
                    && candidate->GetParamCount() == args.Size())
                {
                    target = candidate;
                    break;
                }
            }
            if (target == nullptr) { return core::Err(core::ErrorCode::NotFound); }
            return m_owner->ExecuteCall(target, m_instance, args);
        }

    private:
        core::RefPtr<AngelScriptContext> m_owner;
        asIScriptObject* m_instance;
    };

    core::RefPtr<IScriptContext> AngelScriptManager::CreateContext()
    {
        // Defensive finalize (the documented contract): a context may be created
        // without RegisterReflectedTypes having driven the two-phase emission.
        FinalizeTypes();
        return core::RefPtr<IScriptContext>(core::MakeRef<AngelScriptContext>(
            core::DefaultAllocator(), core::RefPtr<AngelScriptManager>(this), m_nextContextId++));
    }

    core::RefPtr<ScriptObject> AngelScriptContext::CreateInstance(
        core::StringView className, core::Span<core::Variant> args)
    {
        if (m_module == nullptr) { return nullptr; }
        const core::String name(className);
        asITypeInfo* type = m_module->GetTypeInfoByDecl(CStr(name));
        if (type == nullptr) { return nullptr; }
        asIScriptFunction* factory = nullptr;
        for (asUINT i = 0; i < type->GetFactoryCount(); ++i)
        {
            asIScriptFunction* candidate = type->GetFactoryByIndex(i);
            if (candidate->GetParamCount() != args.Size()) { continue; }
            if (args.Size() == 1)
            {
                // Skip the implicit copy factory (one self-typed parameter) - a
                // Variant argument can never be a live script object.
                int paramTypeId = 0;
                (void)candidate->GetParam(0, &paramTypeId);
                const int baseId = paramTypeId & ~(asTYPEID_OBJHANDLE | asTYPEID_HANDLETOCONST);
                if (baseId == type->GetTypeId()) { continue; }
            }
            factory = candidate;
            break;
        }
        if (factory == nullptr) { return nullptr; }

        asIScriptEngine* engine = m_manager->Engine();
        asIScriptContext* executor = engine->RequestContext();
        if (executor == nullptr || executor->Prepare(factory) < 0)
        {
            if (executor != nullptr) { engine->ReturnContext(executor); }
            return nullptr;
        }
        std::string stringTemps[kMaxArgs];
        BoxedVariant* boxTemps[kMaxArgs] = {};
        BindArgs(executor, factory, args, stringTemps, boxTemps);
        int result;
        {
            ScriptCallScope scope(this);
            result = executor->Execute();
        }
        for (BoxedVariant* box : boxTemps) { ReleaseBox(box); }
        asIScriptObject* instance = nullptr;
        if (result == asEXECUTION_FINISHED)
        {
            instance = static_cast<asIScriptObject*>(executor->GetReturnObject());
            if (instance != nullptr) { instance->AddRef(); } // before the pool reuses the context
        }
        else if (result == asEXECUTION_EXCEPTION)
        {
            ReportException(executor);
        }
        engine->ReturnContext(executor);
        if (instance == nullptr) { return nullptr; }
        return core::RefPtr<ScriptObject>(core::MakeRef<AngelScriptObject>(
            core::DefaultAllocator(), core::RefPtr<AngelScriptContext>(this), instance));
    }

    core::RefPtr<IScriptManager> CreateScriptManager()
    {
        return core::RefPtr<IScriptManager>(
            core::MakeRef<AngelScriptManager>(core::DefaultAllocator()));
    }

    void RegisterAngelScriptBackend()
    {
        ScriptBackendDesc desc;
        desc.languageId = core::String(u8"angelscript");
        desc.displayName = core::String(u8"AngelScript");
        desc.fileExtensions.PushBack(core::String(u8"as"));
        desc.create = []() { return CreateScriptManager(); };
        ScriptBackendRegistry::Get().Register(core::Move(desc));
    }
}
