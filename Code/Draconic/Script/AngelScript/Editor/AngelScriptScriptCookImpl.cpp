// Draconic::ScriptAngelScriptEditor - implementation unit for the AngelScript cook.
//
// Compile-check + shared handler scan; property harvest deferred (see the interface unit).
// No AngelScript SDK header - the cook VM is reached through the neutral IScriptContext.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module draconic.script.angelscript.editor;

import draconic.core;
import draconic.editor.core;   // FileStemOf
import draconic.script;
import draconic.script.resource;
import draconic.script.facades;
import draconic.script.editor;
import draconic.script.angelscript;

using namespace draconic::core;

namespace draconic::script
{
    namespace
    {
        class AngelScriptScriptCook final : public IScriptLanguageCook
        {
        public:
            [[nodiscard]] StringView NewAssetTemplate() const override
            {
                return kAngelScriptBehaviorStarter;
            }

            [[nodiscard]] bool Cook(StringView source, StringView assetName,
                                    CookScriptErrorSink& sink, ScriptClassSource& out) override
            {
                out.language = String(u8"angelscript");
                out.source = String(source);

                RefPtr<IScriptManager> manager = CreateScriptManagerForLanguage(u8"angelscript");
                if (manager.Get() == nullptr)
                {
                    DRACONIC_LOG_ERROR(u8"Script",
                        u8"'{}': no AngelScript backend registered - cook failed", assetName);
                    return false;
                }
                // Same "main"-module surface the runtime registers (core + facades), so a
                // facade-using behavior compiles at cook exactly as at runtime. Idempotent.
                RegisterCoreTypes();
                RegisterScriptFacadeReflection();
                RegisterReflectedTypes(*manager);
                RefPtr<IScriptContext> context = manager->CreateContext();
                if (context.Get() == nullptr) { return false; }
                context->SetErrorHandler(&sink);

                // AngelScript needs no import prelude (assemble adds none), so cook error
                // lines already match the authored file - no prelude offset to subtract.
                const StringView single[] = { source };
                const String compileSource = manager->AssembleBehaviorModuleSource(
                    Span<const StringView>{ single, 1 });
                if (!context->Load(compileSource.AsView(), assetName).IsOk())
                {
                    ReportScriptCookErrors(assetName, sink);
                    return false;
                }

                out.className = FindScriptClassName(
                    source, draconic::editor::FileStemOf(assetName));
                out.handlers = ScanScriptHandlers(source);
                out.usesCoroutines = ScriptReferencesCoroutineStart(source);
                // Property harvest is DEFERRED for AngelScript (no `static properties`
                // surface yet) - cook with NO property metadata. Documented in the header.
                return true;
            }
        };
    }

    void RegisterAngelScriptScriptCook()
    {
        draconic::script::angelscript::RegisterAngelScriptBackend();
        ScriptLanguageCookRegistry::Get().Register(
            String(u8"angelscript"),
            UniquePtr<IScriptLanguageCook>(DefaultAllocator().New<AngelScriptScriptCook>(),
                                           DefaultAllocator()));
    }
}
