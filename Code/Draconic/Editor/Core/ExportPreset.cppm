// Draconic::EditorCore - :export_preset partition.
//
// Export presets: named, per-platform descriptions of how to produce a shippable dist - which
// export template (player + runtime sidecars) to use, plus game-specific extra files and output
// naming. Project-local and committable (references a template by id/platform, never an absolute
// path). Persisted to <project>/export_presets.xml (ProjectSettings-shape versioned XML). The
// RaptorExport CLI and the editor's Export menu both drive the export from these, so a preset
// produces the SAME dist whichever surface triggers it (the cook uniformity, extended to the whole
// dist).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor.core:export_preset;

import draconic.core;
import draconic.vfs;
import draconic.xml.serialization;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace vfs = draconic::vfs;

    inline constexpr StringView kExportPresetsFile = u8"export_presets.xml";

    // One export target. A plain value type (copyable/movable) so a project can hold an
    // Array<ExportPreset>; only the root set below is a versioned-payload object.
    //
    // References an ExportTemplate (see :export_template) rather than an absolute path, so a preset is
    // portable/committable: `templateId` picks a template exactly, or blank => the installed template
    // for `platform` (the host implicit template counts). `additionalFiles` are GAME-specific extras
    // (icon, config, data) staged into the dist ON TOP of the template's own runtime sidecars.
    struct ExportPreset
    {
        String name;                    // "Windows Desktop"
        String platform;                // "Win64" / "Linux64" (the Bin/<Config>/<Platform> tag)
        String templateId;              // which template; "" => resolve by platform
        String playerName;              // output exe name; "" => the template's player basename
        String outputSubdir;            // export-root-relative output dir; "" => sanitized `name`
        Array<String> additionalFiles;  // game-specific extra files (beyond the template's sidecars)

        void Serialize(ISerializer& ar)
        {
            draconic::core::Serialize(ar, "name", name);
            draconic::core::Serialize(ar, "platform", platform);
            draconic::core::Serialize(ar, "templateId", templateId);
            draconic::core::Serialize(ar, "playerName", playerName);
            draconic::core::Serialize(ar, "outputSubdir", outputSubdir);
            draconic::core::Serialize(ar, "additionalFiles", additionalFiles);
        }
    };

    // ADL hook so Serialize(ar, Array<ExportPreset>&) resolves each element to the member above.
    // Frames each element as its own object node (the generic Array<T> helper does not), so the
    // per-element keys stay separated on read. Declared before ExportPresetSet so it is visible at
    // that template's instantiation point.
    inline void Serialize(ISerializer& ar, ExportPreset& p)
    {
        ar.BeginObject();
        p.Serialize(ar);
        ar.EndObject();
    }

    // A project's set of export presets - the root, versioned payload of export_presets.xml.
    class ExportPresetSet final : public ISerializable
    {
        DRACONIC_OBJECT(ExportPresetSet, ISerializable)
    public:
        Array<ExportPreset> presets;

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "presets", presets);
        }

        // Find a preset by name (case-sensitive); null when absent.
        [[nodiscard]] const ExportPreset* Find(StringView presetName) const
        {
            for (const ExportPreset& p : presets) { if (p.name.AsView() == presetName) { return &p; } }
            return nullptr;
        }
    };

    // Fill `out` with the built-in default: one preset targeting the host platform, sourcing the
    // player from the driving tool's own directory. Used when a project has no export_presets.xml.
    inline void DefaultExportPresets(ExportPresetSet& out)
    {
        ExportPreset host;
        host.platform = String(GetHostPlatformName());
        host.name = host.platform;
        host.name += u8" Desktop";
        host.outputSubdir = host.platform;
        out.presets.Clear();
        out.presets.PushBack(Move(host));
    }

    // Read export_presets.xml from `root`. NotFound when absent (caller falls back to defaults).
    [[nodiscard]] inline Status LoadExportPresets(vfs::IFileSystem& root, ExportPresetSet& out,
                                                  StringView fileName = kExportPresetsFile)
    {
        UniquePtr<IStream> stream = root.Open(fileName, FileMode::Read);
        if (!stream) { return Status{ ErrorCode::NotFound }; }
        SerializerFactory factory = draconic::xml::XmlSerializerFactory();
        UniquePtr<SerializerContext> ctx = factory(*stream, SerializeMode::Read);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }
        BeginVersionedPayload(*ctx->serializer, ExportPresetSet::StaticType());
        out.Serialize(*ctx->serializer);
        EndVersionedPayload(*ctx->serializer);
        return ctx->serializer->IsOk() ? Status{} : ctx->serializer->GetStatus();
    }

    // Write export_presets.xml to `root`.
    [[nodiscard]] inline Status SaveExportPresets(vfs::IWritableFileSystem& writable,
                                                  ExportPresetSet& presets,
                                                  StringView fileName = kExportPresetsFile)
    {
        MemoryStream buffer;
        SerializerFactory factory = draconic::xml::XmlSerializerFactory();
        UniquePtr<SerializerContext> ctx = factory(buffer, SerializeMode::Write);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }
        BeginVersionedPayload(*ctx->serializer, ExportPresetSet::StaticType());
        presets.Serialize(*ctx->serializer);
        EndVersionedPayload(*ctx->serializer);
        if (!ctx->serializer->IsOk()) { return ctx->serializer->GetStatus(); }
        ctx->Flush(buffer);
        return writable.Save(fileName, buffer.Bytes());
    }

    DRACONIC_DEFINE_OBJECT_VERSIONED(ExportPresetSet, "draconic::editor", 1)
}
