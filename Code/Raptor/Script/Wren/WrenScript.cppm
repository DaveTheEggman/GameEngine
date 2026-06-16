// Raptor::ScriptWren — Wren VM backend (raptor.script.wren).
//
// Implements Raptor::Script's IScriptManager / IScriptContext on the Wren VM.
// A WrenContext owns one WrenVM (Wren's VM is the isolated environment). This
// first slice runs source and reports compile/runtime errors; reflection-driven
// foreign-class binding (exposing reflected types to Wren), globals, and Call
// land in later slices.

module;
#include "Core/Prelude.h"
#include "WrenInclude.h"

export module raptor.script.wren;

import raptor.core;
import raptor.script;

namespace rc = raptor::core;

namespace raptor::script::wren
{
    // Routes a Wren UTF-8 cstring to a wide ConsoleWrite (transcode at the edge).
    inline void WriteUtf8(void (*sink)(rc::StringView), const char* text)
    {
        if (text != nullptr)
        {
            sink(rc::ToWide(rc::UTF8StringView(reinterpret_cast<const rc::utf8char*>(text))).AsView());
        }
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
            const WrenInterpretResult result = wrenInterpret(
                m_vm,
                reinterpret_cast<const char*>(name.CStr()),
                reinterpret_cast<const char*>(src.CStr()));
            switch (result)
            {
                case WREN_RESULT_SUCCESS:       return rc::Status{};
                case WREN_RESULT_COMPILE_ERROR: return rc::Status{ rc::ErrorCode::InvalidArgument };
                case WREN_RESULT_RUNTIME_ERROR: return rc::Status{ rc::ErrorCode::Internal };
            }
            return rc::Status{ rc::ErrorCode::Unknown };
        }

        // Globals / Call: reflection binding lands in a later slice.
        void SetGlobal(rc::StringView, const rc::Variant&) override {}
        [[nodiscard]] rc::Variant GetGlobal(rc::StringView) override { return rc::Variant{}; }
        [[nodiscard]] bool HasFunction(rc::StringView) const override { return false; }
        [[nodiscard]] rc::Result<rc::Variant> Call(rc::StringView, rc::Span<rc::Variant>) override
        {
            return rc::Err(rc::ErrorCode::NotSupported);
        }

    private:
        static void OnWrite(WrenVM*, const char* text) { WriteUtf8(&rc::ConsoleWrite, text); }
        static void OnError(WrenVM*, WrenErrorType, const char*, int, const char* message)
        {
            WriteUtf8(&rc::ConsoleWriteError, message);
        }

        WrenVM* m_vm = nullptr;
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
