// Draconic::ScriptEditor - the `draconic.script.editor` module (tooling).
//
// Source-side script authoring + cook (docs/design/scripting.md §5 + §7.5 B3):
//   * ScriptClassAsset (editor::Asset): the copied script file + its LANGUAGE
//     (defaulted from the imported file's extension - backend neutrality B3).
//   * ScriptClassAssetBuilder: compiles the source in a cooker-owned VM resolved
//     through the ScriptBackendRegistry (never a named backend type), surfaces
//     compile errors as cook errors with file/line, harvests the `static properties`
//     map + the declared handler set, and writes source + metadata. A failing cook
//     leaves the LAST good cooked record untouched (the cook service only reloads
//     successful products), so live instances keep running the old class.
//   * ScriptFileImporter: drop-import for any extension a registered backend claims
//     (.wren today); no options dialog.
//
// Never linked by the runtime. The Wren-specific part is confined to the PROPERTY
// probe snippet (other languages cook compile-checked with text-scanned handlers and
// no property metadata until they grow their own probe).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Core/Log/Log.h"

export module draconic.script.editor;

import draconic.core;
import draconic.editor;
import draconic.editor.core;
import draconic.content;
import draconic.script;
import draconic.script.resource;
import draconic.script.facades;   // the cook VM mirrors the runtime's "main" surface

using namespace draconic::core;

export namespace draconic::script
{
    namespace content = draconic::content;

    // Source asset: the script file + the backend that compiles it.
    class ScriptClassAsset final : public draconic::editor::Asset
    {
        DRACONIC_OBJECT(ScriptClassAsset, draconic::editor::Asset)
    public:
        String language;   // backend id ("wren"), defaulted from the file extension

        void Serialize(ISerializer& ar) override
        {
            draconic::editor::Asset::Serialize(ar);   // fileName
            draconic::core::Serialize(ar, "language", language);
        }
    };

    // ---- pure cook helpers (unit-testable without a project) ----

    /// First top-level `class Name` whose name matches `preferredName` (the file stem),
    /// else the FIRST top-level class; empty when the source declares none (a utility
    /// module). Line comments / block comments are ignored.
    [[nodiscard]] inline String FindScriptClassName(StringView source, StringView preferredName);

    /// The declared lifecycle handlers out of the KNOWN set, by source scan (comments
    /// stripped): a handler is `name(` or `name {` at declaration position.
    [[nodiscard]] inline Array<String> ScanScriptHandlers(StringView source);

    /// Strips `//` line comments and `/* */` block comments (string literals respected).
    [[nodiscard]] inline String StripScriptComments(StringView source);

    /// One record of the Wren probe's harvest string -> a property desc.
    /// Record layout: name \x1F typeString \x1F default \x1F description, where default
    /// is "~" (none), "n:<num>", "b:true|false", "s:<text>" or "l:<a,b,c[,d]>".
    [[nodiscard]] inline bool ParseHarvestRecord(StringView record, ScriptPropertyDesc& out,
                                                 String& outError);

    // The New Asset starter (the behavior convention pre-filled). Property values reach
    // an instance through plain Wren SETTERS (`speed=(v)`) - harvested names are pushed
    // via `Invoke("<name>=")` at instantiate, after construct new(entity).
    inline constexpr StringView kScriptBehaviorStarter =
        u8"// Behavior class - attach via a ScriptComponent behavior slot.\n"
        u8"// NOTE Wren is newline-sensitive: `{` must sit on the signature's line.\n"
        u8"// Reflected engine types live in the \"main\" module:\n"
        u8"//   import \"main\" for Float3\n"
        u8"class NewBehavior {\n"
        u8"    // name: [type, default, description?]  - types: float, int, bool, string,\n"
        u8"    // color, vec3, entity, asset:<TypeName>\n"
        u8"    static properties { {\n"
        u8"        \"speed\": [\"float\", 1.0, \"units per second\"],\n"
        u8"    } }\n"
        u8"\n"
        u8"    construct new(entity) {\n"
        u8"        _entity = entity\n"
        u8"        _speed = 1.0\n"
        u8"    }\n"
        u8"    // One setter per declared property (the engine pushes values through them).\n"
        u8"    speed=(v) { _speed = v }\n"
        u8"\n"
        u8"    onStart() {}\n"
        u8"    onUpdate(dt) {}\n"
        u8"    onDestroy() {}\n"
        u8"}\n";

    namespace detail
    {
        [[nodiscard]] inline bool IsIdentChar(utf8char c) noexcept
        {
            return (c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z')
                || (c >= u8'0' && c <= u8'9') || c == u8'_';
        }

        // The Wren property probe, appended INTO the class's own module so the class
        // name resolves without imports. Wren maps have no guaranteed key order - the
        // cook SORTS the parsed records by name for deterministic bytes.
        [[nodiscard]] inline String BuildWrenPropertyProbe(StringView className)
        {
            String cls(className);
            String probe;
            probe += u8"var drHarvestResult = \"\"\n";
            probe += u8"var drHarvestError = \"\"\n";
            // NOTE Wren parses a single-line `{ ... }` as an EXPRESSION body - every
            // block holding a statement must span multiple lines.
            probe += u8"var drHarvestSer\n";
            probe += u8"drHarvestSer = Fn.new {|v|\n";
            probe += u8"  var out = \"?\"\n";
            probe += u8"  if (v == null) {\n";
            probe += u8"    out = \"~\"\n";
            probe += u8"  } else if (v is Num) {\n";
            probe += u8"    out = \"n:\" + v.toString\n";
            probe += u8"  } else if (v is Bool) {\n";
            probe += u8"    out = \"b:\" + v.toString\n";
            probe += u8"  } else if (v is String) {\n";
            probe += u8"    out = \"s:\" + v\n";
            probe += u8"  } else if (v is List) {\n";
            probe += u8"    var parts = \"\"\n";
            probe += u8"    for (e in v) {\n";
            probe += u8"      if (parts != \"\") parts = parts + \",\"\n";
            probe += u8"      parts = parts + e.toString\n";
            probe += u8"    }\n";
            probe += u8"    out = \"l:\" + parts\n";
            probe += u8"  }\n";
            probe += u8"  return out\n";
            probe += u8"}\n";
            probe += u8"var drHarvestFiber = Fiber.new {\n";
            probe += u8"  var m = ";
            probe += cls.AsView();
            probe += u8".properties\n";
            probe += u8"  var out = \"\"\n";
            probe += u8"  for (k in m.keys) {\n";
            probe += u8"    var entry = m[k]\n";
            probe += u8"    var type = \"\"\n";
            probe += u8"    var dflt = \"~\"\n";
            probe += u8"    var desc = \"\"\n";
            probe += u8"    if (entry is List) {\n";
            probe += u8"      if (entry.count > 0) { type = entry[0].toString }\n";
            probe += u8"      if (entry.count > 1) { dflt = drHarvestSer.call(entry[1]) }\n";
            probe += u8"      if (entry.count > 2) { desc = entry[2].toString }\n";
            probe += u8"    } else {\n";
            probe += u8"      type = entry.toString\n";
            probe += u8"    }\n";
            probe += u8"    out = out + k + \"\\x1f\" + type + \"\\x1f\" + dflt + \"\\x1f\" + desc + \"\\x1e\"\n";
            probe += u8"  }\n";
            probe += u8"  drHarvestResult = out\n";
            probe += u8"}\n";
            probe += u8"var drHarvestCaught = drHarvestFiber.try()\n";
            probe += u8"if (drHarvestCaught != null) { drHarvestError = drHarvestCaught.toString }\n";
            return probe;
        }

        // Parses "a,b,c[,d]" into up to 4 floats; returns the count parsed.
        [[nodiscard]] inline u32 ParseFloatList(StringView text, f32 (&out)[4])
        {
            u32 count = 0;
            usize begin = 0;
            for (usize i = 0; i <= text.Size() && count < 4; ++i)
            {
                if (i == text.Size() || text[i] == u8',')
                {
                    const StringView piece = text.SubStr(begin, i - begin);
                    begin = i + 1;
                    if (piece.IsEmpty()) { continue; }
                    // Minimal float parse (sign, digits, dot, exponent-free harvest output).
                    f64 value = 0.0;
                    f64 scale = 1.0;
                    bool negative = false;
                    bool afterDot = false;
                    for (usize j = 0; j < piece.Size(); ++j)
                    {
                        const utf8char c = piece[j];
                        if (j == 0 && c == u8'-') { negative = true; continue; }
                        if (c == u8'.') { afterDot = true; continue; }
                        if (c < u8'0' || c > u8'9') { continue; }
                        if (afterDot) { scale *= 0.1; value += (c - u8'0') * scale; }
                        else { value = value * 10.0 + (c - u8'0'); }
                    }
                    out[count++] = static_cast<f32>(negative ? -value : value);
                }
            }
            return count;
        }
    }

    inline String StripScriptComments(StringView source)
    {
        String out;
        out.Reserve(source.Size());
        bool inString = false;
        bool inLineComment = false;
        bool inBlockComment = false;
        for (usize i = 0; i < source.Size(); ++i)
        {
            const utf8char c = source[i];
            const utf8char next = (i + 1 < source.Size()) ? source[i + 1] : utf8char(0);
            if (inLineComment)
            {
                if (c == u8'\n') { inLineComment = false; out.PushBack(c); }
                continue;
            }
            if (inBlockComment)
            {
                if (c == u8'*' && next == u8'/') { inBlockComment = false; ++i; }
                else if (c == u8'\n') { out.PushBack(c); }   // keep line numbers stable-ish
                continue;
            }
            if (inString)
            {
                if (c == u8'\\') { out.PushBack(c); if (next != 0) { out.PushBack(next); ++i; } continue; }
                if (c == u8'"') { inString = false; }
                out.PushBack(c);
                continue;
            }
            if (c == u8'"') { inString = true; out.PushBack(c); continue; }
            if (c == u8'/' && next == u8'/') { inLineComment = true; ++i; continue; }
            if (c == u8'/' && next == u8'*') { inBlockComment = true; ++i; continue; }
            out.PushBack(c);
        }
        return out;
    }

    inline String FindScriptClassName(StringView source, StringView preferredName)
    {
        const String stripped = StripScriptComments(source);
        const StringView text = stripped.AsView();
        String first;
        bool lineStart = true;
        for (usize i = 0; i < text.Size(); ++i)
        {
            const utf8char c = text[i];
            if (c == u8'\n') { lineStart = true; continue; }
            if (lineStart && (c == u8' ' || c == u8'\t')) { continue; }
            if (lineStart)
            {
                lineStart = false;
                const StringView keyword = u8"class ";
                if (i + keyword.Size() < text.Size()
                    && text.SubStr(i, keyword.Size()) == keyword)
                {
                    usize begin = i + keyword.Size();
                    while (begin < text.Size() && text[begin] == u8' ') { ++begin; }
                    usize end = begin;
                    while (end < text.Size() && detail::IsIdentChar(text[end])) { ++end; }
                    if (end > begin)
                    {
                        const StringView name = text.SubStr(begin, end - begin);
                        if (name == preferredName) { return String(name); }
                        if (first.IsEmpty()) { first = String(name); }
                    }
                }
            }
        }
        return first;
    }

    inline Array<String> ScanScriptHandlers(StringView source)
    {
        static constexpr StringView kKnownHandlers[] = {
            u8"onStart", u8"onUpdate", u8"onFixedUpdate",
            u8"onEnable", u8"onDisable", u8"onDestroy",
        };
        const String stripped = StripScriptComments(source);
        const StringView text = stripped.AsView();
        Array<String> found;
        for (StringView handler : kKnownHandlers)
        {
            bool present = false;
            for (usize i = 0; !present && i + handler.Size() < text.Size(); ++i)
            {
                if (text.SubStr(i, handler.Size()) != handler) { continue; }
                if (i > 0 && detail::IsIdentChar(text[i - 1])) { continue; }
                usize after = i + handler.Size();
                while (after < text.Size() && (text[after] == u8' ' || text[after] == u8'\t'))
                {
                    ++after;
                }
                if (after < text.Size() && (text[after] == u8'(' || text[after] == u8'{'))
                {
                    present = true;
                }
            }
            if (present) { found.PushBack(String(handler)); }
        }
        return found;
    }

    inline bool ParseHarvestRecord(StringView record, ScriptPropertyDesc& out, String& outError)
    {
        StringView fields[4];
        u32 fieldCount = 0;
        usize begin = 0;
        for (usize i = 0; i <= record.Size() && fieldCount < 4; ++i)
        {
            if (i == record.Size() || record[i] == utf8char(0x1F))
            {
                fields[fieldCount++] = record.SubStr(begin, i - begin);
                begin = i + 1;
            }
        }
        if (fieldCount < 2 || fields[0].IsEmpty())
        {
            outError = String(u8"malformed property record");
            return false;
        }
        out.name = String(fields[0]);
        out.hash = ScriptPropertyNameHash(fields[0]);
        out.description = fieldCount > 3 ? String(fields[3]) : String{};
        if (!ParseScriptPropertyType(fields[1], out.type, out.assetType))
        {
            outError = String(u8"property '");
            outError += fields[0];
            outError += u8"' has unknown type '";
            outError += fields[1];
            outError += u8"' (valid: float, int, bool, string, color, vec3, entity, asset:<TypeName>)";
            return false;
        }

        // Default value: typed from the serialized payload; "~" = the type's default.
        ScriptPropertyValue& value = out.defaultValue;
        value.kind = out.type;
        const StringView payload = fieldCount > 2 ? fields[2] : StringView(u8"~");
        if (payload == u8"~" || payload.Size() < 2) { return true; }
        const utf8char tag = payload[0];
        const StringView body = payload.SubStr(2, payload.Size() - 2);
        switch (out.type)
        {
            case ScriptPropertyType::Float:
            case ScriptPropertyType::Int:
                if (tag == u8'n')
                {
                    f32 numbers[4] = {};
                    if (detail::ParseFloatList(body, numbers) > 0)
                    {
                        value.number = static_cast<f64>(numbers[0]);
                        if (out.type == ScriptPropertyType::Int)
                        {
                            value.number = static_cast<f64>(static_cast<i64>(value.number));
                        }
                    }
                }
                break;
            case ScriptPropertyType::Bool:
                value.boolean = (tag == u8'b' && body == u8"true");
                break;
            case ScriptPropertyType::String:
                if (tag == u8's') { value.text = String(body); }
                break;
            case ScriptPropertyType::Color:
                if (tag == u8'l')
                {
                    f32 numbers[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                    const u32 parsed = detail::ParseFloatList(body, numbers);
                    if (parsed >= 3)
                    {
                        value.color = Color{ numbers[0], numbers[1], numbers[2],
                                             parsed >= 4 ? numbers[3] : 1.0f };
                    }
                }
                break;
            case ScriptPropertyType::Vec3:
                if (tag == u8'l')
                {
                    f32 numbers[4] = {};
                    if (detail::ParseFloatList(body, numbers) >= 3)
                    {
                        value.vector = Float3{ numbers[0], numbers[1], numbers[2] };
                    }
                }
                break;
            case ScriptPropertyType::Entity:
            case ScriptPropertyType::Asset:
                // Only null defaults are expressible in script; guids come from overrides.
                break;
            case ScriptPropertyType::None:
            default:
                break;
        }
        return true;
    }

    // Captures compile/runtime errors during the harvest compile (file/line for the
    // cook error report - ScriptError already carries them).
    class CookScriptErrorSink final : public IScriptErrorHandler
    {
    public:
        struct Entry
        {
            ScriptErrorKind kind;
            String module;
            i32 line;
            String message;
        };
        Array<Entry> errors;

        void OnError(const ScriptError& error) override
        {
            Entry entry{ error.kind, String(error.module), error.line, String(error.message) };
            errors.PushBack(Move(entry));
        }
    };

    // Cooks a ScriptClassAsset -> ScriptClassSource (compile + harvest, B3: the VM is
    // resolved from the asset's LANGUAGE through the backend registry).
    class ScriptClassAssetBuilder final : public draconic::editor::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &ScriptClassAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &ScriptClassSource::StaticType();
        }
        [[nodiscard]] u32 Version() const override { return 1; }

        [[nodiscard]] Status Build(const draconic::editor::Asset& asset,
                                   draconic::editor::AssetBuildContext& ctx) override
        {
            const ScriptClassAsset& scriptAsset = static_cast<const ScriptClassAsset&>(asset);
            if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }

            String source;
            const Status read = ReadSourceText(ctx, scriptAsset.fileName.AsView(), source);
            if (!read.IsOk())
            {
                DRACONIC_LOG_ERROR(u8"Script", u8"'{}': source file missing - cook failed",
                                   scriptAsset.fileName);
                return read;
            }

            const StringView language = scriptAsset.language.IsEmpty()
                ? StringView(u8"wren") : scriptAsset.language.AsView();

            // B3: the harvest VM comes from the registry, by LANGUAGE - never a named
            // backend type. No backend = a configuration error, surfaced as a cook error.
            RefPtr<IScriptManager> manager = CreateScriptManagerForLanguage(language);
            if (manager.Get() == nullptr)
            {
                DRACONIC_LOG_ERROR(u8"Script",
                    u8"'{}': no script backend registered for language '{}' - cook failed",
                    scriptAsset.fileName, language);
                return Status{ ErrorCode::NotSupported };
            }
            // The cook VM registers the SAME "main"-module surface the runtime does
            // (core math + the behavior facades), so the prelude import and explicit
            // `import "main" for Float3`-style imports compile identically here.
            RegisterCoreTypes();
            RegisterScriptFacadeReflection();
            RegisterReflectedTypes(*manager);
            RefPtr<IScriptContext> context = manager->CreateContext();
            if (context.Get() == nullptr) { return Status{ ErrorCode::Internal }; }
            CookScriptErrorSink sink;
            context->SetErrorHandler(&sink);

            // Compile check: errors become cook errors with the asset's file + line.
            // Wren sources compile WITH the runtime's one-line facade prelude (error
            // lines shift by one; the reporter subtracts it back).
            const bool wrenPrelude = (language == u8"wren");
            String compileSource;
            if (wrenPrelude) { compileSource += kScriptBehaviorModulePrelude; }
            compileSource += source.AsView();
            if (!context->Load(compileSource.AsView(), scriptAsset.fileName.AsView()).IsOk())
            {
                ReportCompileErrors(scriptAsset.fileName.AsView(), sink, wrenPrelude ? 1 : 0);
                return Status{ ErrorCode::InvalidArgument };
            }

            ScriptClassSource cooked;
            cooked.language = String(language);
            cooked.source = source;
            cooked.className = FindScriptClassName(
                source.AsView(),
                draconic::editor::FileStemOf(scriptAsset.fileName.AsView()));
            cooked.handlers = ScanScriptHandlers(source.AsView());

            // Probe only when the convention is DECLARED (a class without a
            // `static properties` getter legitimately has no inspector rows).
            if (!cooked.className.IsEmpty() && language == u8"wren"
                && DeclaresStaticProperties(source.AsView()))
            {
                const Status harvested = HarvestWrenProperties(
                    *context, scriptAsset.fileName.AsView(), cooked.className.AsView(),
                    sink, cooked.properties);
                if (!harvested.IsOk()) { return harvested; }
            }
            return ctx.output->WriteObject(cooked);
        }

    private:
        // Whether the (comment-stripped) source declares the `static properties` getter.
        [[nodiscard]] static bool DeclaresStaticProperties(StringView source)
        {
            const String stripped = StripScriptComments(source);
            const StringView text = stripped.AsView();
            const StringView keyword = u8"static properties";
            if (text.Size() < keyword.Size()) { return false; }
            for (usize i = 0; i + keyword.Size() <= text.Size(); ++i)
            {
                if (text.SubStr(i, keyword.Size()) == keyword) { return true; }
            }
            return false;
        }

        static void ReportCompileErrors(StringView fileName, const CookScriptErrorSink& sink,
                                        i32 preludeLines = 0)
        {
            if (sink.errors.IsEmpty())
            {
                DRACONIC_LOG_ERROR(u8"Script", u8"'{}': compile failed - cook failed", fileName);
                return;
            }
            for (const CookScriptErrorSink::Entry& e : sink.errors)
            {
                const i32 line = e.line > preludeLines ? e.line - preludeLines : e.line;
                DRACONIC_LOG_ERROR(u8"Script", u8"{}:{}: {} - cook failed",
                                   e.module.IsEmpty() ? fileName : e.module.AsView(),
                                   line, e.message);
            }
        }

        // Appends the probe into the class's own module (same chunk name = same Wren
        // module, so the class resolves), then reads the harvest globals back.
        [[nodiscard]] static Status HarvestWrenProperties(
            IScriptContext& context, StringView fileName, StringView className,
            CookScriptErrorSink& sink, Array<ScriptPropertyDesc>& outProperties)
        {
            const String probe = detail::BuildWrenPropertyProbe(className);
            if (!context.Load(probe.AsView(), fileName).IsOk())
            {
                ReportCompileErrors(fileName, sink);
                return Status{ ErrorCode::InvalidArgument };
            }
            const Variant errorVariant = context.GetGlobal(u8"drHarvestError");
            if (const String* probeError = errorVariant.TryGet<String>();
                probeError != nullptr && !probeError->IsEmpty())
            {
                DRACONIC_LOG_ERROR(u8"Script",
                    u8"'{}': `static properties` of class '{}' faulted: {} - cook failed",
                    fileName, className, *probeError);
                return Status{ ErrorCode::InvalidArgument };
            }
            const Variant resultVariant = context.GetGlobal(u8"drHarvestResult");
            const String* harvest = resultVariant.TryGet<String>();
            if (harvest == nullptr) { return Status{}; }   // no properties getter at all

            const StringView text = harvest->AsView();
            usize begin = 0;
            for (usize i = 0; i < text.Size(); ++i)
            {
                if (text[i] != utf8char(0x1E)) { continue; }
                const StringView record = text.SubStr(begin, i - begin);
                begin = i + 1;
                if (record.IsEmpty()) { continue; }
                ScriptPropertyDesc desc;
                String error;
                if (!ParseHarvestRecord(record, desc, error))
                {
                    DRACONIC_LOG_ERROR(u8"Script", u8"'{}': {} - cook failed", fileName, error);
                    return Status{ ErrorCode::InvalidArgument };
                }
                outProperties.PushBack(Move(desc));
            }
            // Wren map order is unspecified: sort by name for deterministic cooked bytes.
            auto lessThan = [](StringView a, StringView b) {
                const usize n = a.Size() < b.Size() ? a.Size() : b.Size();
                for (usize k = 0; k < n; ++k)
                {
                    if (a[k] != b[k]) { return a[k] < b[k]; }
                }
                return a.Size() < b.Size();
            };
            for (usize i = 1; i < outProperties.Size(); ++i)
            {
                for (usize j = i; j > 0
                     && lessThan(outProperties[j].name.AsView(), outProperties[j - 1].name.AsView()); --j)
                {
                    ScriptPropertyDesc tmp = Move(outProperties[j]);
                    outProperties[j] = Move(outProperties[j - 1]);
                    outProperties[j - 1] = Move(tmp);
                }
            }
            return Status{};
        }
    };

    // OS-file importer (editor drag-drop): accepts any extension a REGISTERED script
    // backend claims (B3 - language-clean), copies the file into Sources/, and creates
    // a ScriptClassAsset whose language records the owning backend. No options dialog.
    class ScriptFileImporter final : public draconic::editor::IFileImporter
    {
    public:
        [[nodiscard]] StringView Label() const override { return u8"Script"; }

        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            return ScriptBackendRegistry::Get().FindByExtension(extension) != nullptr;
        }

        [[nodiscard]] RefPtr<draconic::editor::ImportOptions> CreateOptions() const override
        {
            return {};   // no options dialog - the drop imports immediately
        }

        [[nodiscard]] Result<content::Instance*> Import(
            StringView sourcePath, draconic::editor::EditorProject& project,
            content::Group& group, const draconic::editor::ImportOptions*,
            Object*, Array<draconic::editor::DeferredImportWrite>*) override
        {
            const String extension = draconic::editor::FileExtensionLower(sourcePath);
            const ScriptBackendDesc* backend =
                ScriptBackendRegistry::Get().FindByExtension(extension.AsView());
            if (backend == nullptr) { return Err(ErrorCode::NotSupported); }

            Result<String> fileName = draconic::editor::CopyIntoSources(project, sourcePath);
            if (!fileName.HasValue()) { return Err(fileName.Error()); }

            const StringView stem = draconic::editor::FileStemOf(fileName.Value().AsView());
            content::Instance* instance =
                group.CreateInstance(stem, ScriptClassAsset::StaticType());
            if (instance == nullptr) { return Err(ErrorCode::Unknown); }

            ScriptClassAsset asset;
            asset.fileName = fileName.Value();
            asset.language = String(backend->languageId.AsView());
            const Status written = instance->WriteObject(asset);
            if (!written.IsOk()) { return Err(written.Code()); }
            return instance;
        }
    };

    // Registers the asset type for content-DB construction + deserialization.
    inline void RegisterScriptAssets()
    {
        GlobalTypeRegistry().Register(ScriptClassAsset::StaticType());
        RegisterSerializable<ScriptClassAsset>();
    }

    DRACONIC_DEFINE_OBJECT(ScriptClassAsset, "draconic::script")
}
