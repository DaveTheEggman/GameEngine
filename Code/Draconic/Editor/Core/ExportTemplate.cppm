// Draconic::EditorCore - :export_template partition.
//
// Export templates: portable, per-platform prebuilt bundles (a player binary + its runtime sidecars +
// a template.xml manifest) that presets reference by id/platform (docs/design/export.md §2). They live
// in a machine-local templates root (not committed), are importable/downloadable, and are decoupled
// from any one machine's paths. The HOST implicit template is synthesized from the running tool's own
// directory (Bin/...), so a dev export for the current platform needs zero setup.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor.core:export_template;

import draconic.core;
import draconic.vfs;
import draconic.xml.serialization;
import draconic.project;
import :export_preset;   // ExportPreset, ExportPresetSet

using namespace draconic::core;

export namespace draconic::editor
{
    namespace vfs = draconic::vfs;

    inline constexpr StringView kTemplateManifestFile = u8"template.xml";

    // A prebuilt per-platform export bundle. The serialized fields come from template.xml; `directory`
    // and `isHost` are runtime-resolved (the registry sets them) and NOT serialized. ISerializable so
    // template.xml round-trips as a versioned payload; the registry owns instances via UniquePtr.
    class ExportTemplate final : public ISerializable
    {
        DRACONIC_OBJECT(ExportTemplate, ISerializable)
    public:
        String id;              // "raptor-win64-0.1.0" (unique within the templates root)
        String name;            // "Windows Desktop 0.1.0"
        String platform;        // "Win64" / "Linux64"
        String engineVersion;   // engine this was built against (soft-matched; warn on mismatch)
        String playerBinary;    // player exe filename within the template dir
        Array<String> sidecars; // runtime files (relative to the template dir) staged beside the player
        String notes;

        String directory;       // NOT serialized: absolute dir the bundle lives in (host: the Bin dir)
        bool isHost = false;    // NOT serialized: synthesized host template vs imported from disk

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "id", id);
            draconic::core::Serialize(ar, "name", name);
            draconic::core::Serialize(ar, "platform", platform);
            draconic::core::Serialize(ar, "engineVersion", engineVersion);
            draconic::core::Serialize(ar, "playerBinary", playerBinary);
            draconic::core::Serialize(ar, "sidecars", sidecars);
            draconic::core::Serialize(ar, "notes", notes);
        }
    };

    // Read a template.xml (relative path `fileName`) from `root`. NotFound when absent.
    [[nodiscard]] inline Status LoadTemplateManifest(vfs::IFileSystem& root, ExportTemplate& out,
                                                     StringView fileName = kTemplateManifestFile)
    {
        UniquePtr<IStream> stream = root.Open(fileName, FileMode::Read);
        if (!stream) { return Status{ ErrorCode::NotFound }; }
        SerializerFactory factory = draconic::xml::XmlSerializerFactory();
        UniquePtr<SerializerContext> ctx = factory(*stream, SerializeMode::Read);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }
        BeginVersionedPayload(*ctx->serializer, ExportTemplate::StaticType());
        out.Serialize(*ctx->serializer);
        EndVersionedPayload(*ctx->serializer);
        return ctx->serializer->IsOk() ? Status{} : ctx->serializer->GetStatus();
    }

    // Write a template.xml to `root`.
    [[nodiscard]] inline Status SaveTemplateManifest(vfs::IWritableFileSystem& writable, ExportTemplate& tmpl,
                                                     StringView fileName = kTemplateManifestFile)
    {
        MemoryStream buffer;
        SerializerFactory factory = draconic::xml::XmlSerializerFactory();
        UniquePtr<SerializerContext> ctx = factory(buffer, SerializeMode::Write);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }
        BeginVersionedPayload(*ctx->serializer, ExportTemplate::StaticType());
        tmpl.Serialize(*ctx->serializer);
        EndVersionedPayload(*ctx->serializer);
        if (!ctx->serializer->IsOk()) { return ctx->serializer->GetStatus(); }
        ctx->Flush(buffer);
        return writable.Save(fileName, buffer.Bytes());
    }

    // Synthesize the host implicit template from the running tool's own directory (Bin/...), so a dev
    // export for the current platform works with no import.
    // The player target's base name (no exe extension); "<base>.runtime-libs" is the build-emitted
    // sidecar list beside it (written by draconic_copy_runtime_deps).
    inline constexpr StringView kPlayerBaseName = u8"RaptorPlayer";

    // Read a "<name>.runtime-libs" list (one library basename per line) from `fs` into `out`, skipping
    // blank lines and trimming trailing CR/whitespace. Absent/empty file => no entries added.
    inline void ReadRuntimeLibs(vfs::IFileSystem& fs, StringView fileName, Array<String>& out)
    {
        UniquePtr<IStream> stream = fs.Open(fileName, FileMode::Read);
        if (!stream) { return; }
        const usize size = static_cast<usize>(stream->Size());
        if (size == 0) { return; }
        Array<byte> bytes;
        bytes.Resize(size);
        if (stream->Read(bytes.Data(), size) != size) { return; }

        const auto isSpace = [](byte b) { return b == static_cast<byte>(' ') || b == static_cast<byte>('\t')
                                              || b == static_cast<byte>('\r'); };
        usize start = 0;
        for (usize i = 0; i <= size; ++i)
        {
            if (i != size && bytes[i] != static_cast<byte>('\n')) { continue; }
            usize s = start, e = i;
            while (s < e && isSpace(bytes[s])) { ++s; }
            while (e > s && isSpace(bytes[e - 1])) { --e; }
            if (e > s) { out.PushBack(String(StringView(reinterpret_cast<const utf8char*>(bytes.Data() + s), e - s))); }
            start = i + 1;
        }
    }

    // Synthesize the host implicit template from the running tool's own directory (Bin/...), so a dev
    // export for the current platform works with no import. Sidecars come from the build-emitted
    // "<player>.runtime-libs" in that directory (config-driven; empty on rpath platforms).
    inline void SynthesizeHostTemplate(StringView hostToolDir, vfs::IFileSystem* hostToolFs, ExportTemplate& out)
    {
        out.platform = String(GetHostPlatformName());
        out.id = String(u8"host-"); out.id += out.platform;
        out.name = out.platform; out.name += u8" (host build)";
        out.engineVersion = String(draconic::project::kEngineVersionString);
        out.playerBinary = GetExecutableName(kPlayerBaseName);   // host-based (this template is the host)
        out.directory = String(hostToolDir);
        out.isHost = true;
        if (hostToolFs != nullptr)
        {
            String manifest(kPlayerBaseName);
            manifest += u8".runtime-libs";
            ReadRuntimeLibs(*hostToolFs, manifest.AsView(), out.sidecars);
        }
    }

    // The default templates root: <user-data-dir>/templates. The editor may override it (settings);
    // the full resolution order (env / setting / tool-relative) lands with the CLI/editor wiring.
    [[nodiscard]] inline String DefaultTemplatesRoot()
    {
        return PathJoin(GetUserDataDirectory(u8"draconic").AsView(), u8"templates");
    }

    // The installed export templates (imported bundles under a templates root) plus the synthesized
    // host template. Resolves a preset to the template that will produce its dist (export.md §6).
    class TemplateRegistry
    {
    public:
        // Rebuild the set. Imported templates come from `templatesRootFs` (each immediate subdir's
        // template.xml); `templatesRootPath` is that root's absolute path (used to record each
        // template's on-disk directory). Either may be empty/null (=> host template only). The host
        // template is synthesized from `hostToolDir`, reading its sidecars from `hostToolFs` (a VFS
        // rooted at that dir; null => host template with no sidecars).
        void Refresh(StringView templatesRootPath, vfs::IFileSystem* templatesRootFs,
                     StringView hostToolDir, vfs::IFileSystem* hostToolFs)
        {
            m_templates.Clear();

            if (templatesRootFs != nullptr)
            {
                if (vfs::IEnumerableFileSystem* en = templatesRootFs->AsEnumerable())
                {
                    Array<vfs::DirEntry> entries;
                    if (en->Enumerate(u8"", entries).IsOk())
                    {
                        for (const vfs::DirEntry& entry : entries)
                        {
                            if (!entry.isDirectory) { continue; }
                            const String manifestPath = PathJoin(entry.name.AsView(), kTemplateManifestFile);
                            UniquePtr<ExportTemplate> tmpl = MakeUnique<ExportTemplate>(DefaultAllocator());
                            if (LoadTemplateManifest(*templatesRootFs, *tmpl, manifestPath.AsView()).IsOk())
                            {
                                tmpl->directory = PathJoin(templatesRootPath, entry.name.AsView());
                                tmpl->isHost = false;
                                m_templates.PushBack(static_cast<UniquePtr<ExportTemplate>&&>(tmpl));
                            }
                        }
                    }
                }
            }

            UniquePtr<ExportTemplate> host = MakeUnique<ExportTemplate>(DefaultAllocator());
            SynthesizeHostTemplate(hostToolDir, hostToolFs, *host);
            m_templates.PushBack(static_cast<UniquePtr<ExportTemplate>&&>(host));
        }

        [[nodiscard]] usize Count() const noexcept { return m_templates.Size(); }
        [[nodiscard]] const ExportTemplate* At(usize i) const { return m_templates[i].Get(); }

        [[nodiscard]] const ExportTemplate* FindById(StringView id) const
        {
            for (const UniquePtr<ExportTemplate>& t : m_templates)
            {
                if (t->id.AsView() == id) { return t.Get(); }
            }
            return nullptr;
        }

        // The template for `platform`, preferring a real imported bundle over the host synthesized one.
        [[nodiscard]] const ExportTemplate* FindByPlatform(StringView platform) const
        {
            const ExportTemplate* host = nullptr;
            for (const UniquePtr<ExportTemplate>& t : m_templates)
            {
                if (t->platform.AsView() != platform) { continue; }
                if (t->isHost) { host = t.Get(); }
                else { return t.Get(); }
            }
            return host;
        }

        // export.md §6: an explicit templateId wins; otherwise the installed template for the preset's
        // platform (host template counts). Null when nothing matches (caller: "import a template").
        [[nodiscard]] const ExportTemplate* Resolve(const ExportPreset& preset) const
        {
            if (!preset.templateId.IsEmpty()) { return FindById(preset.templateId.AsView()); }
            return FindByPlatform(preset.platform.AsView());
        }

    private:
        Array<UniquePtr<ExportTemplate>> m_templates;
    };

    DRACONIC_DEFINE_OBJECT_VERSIONED(ExportTemplate, "draconic::editor", 1)
}
