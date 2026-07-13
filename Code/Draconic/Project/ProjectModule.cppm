// Draconic::Project - the `draconic.project` module.
//
// The RUNTIME-side project definition: the manifest payload (ProjectSettings), the fixed
// directory layout, and manifest load/save over a VFS root. Split out of the editor so
// shipping binaries (RaptorPlayer, dist builds) carry ZERO editor code - the editor's
// EditorProject builds on top of this (mounts, DBs, per-user state stay editor-side).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.project;

import draconic.core;
import draconic.vfs;
import draconic.xml.serialization;

using namespace draconic::core;

export namespace draconic::project
{
    // The ENGINE version (distinct from per-type data versions): stamped into every saved
    // project manifest so tooling - the editor today, the launcher/project manager later -
    // knows which engine authored a project and can route/migrate accordingly.
    inline constexpr u32 kEngineVersionMajor = 0;
    inline constexpr u32 kEngineVersionMinor = 1;
    inline constexpr u32 kEngineVersionPatch = 0;
    inline constexpr StringView kEngineVersionString = u8"0.1.0";

    inline constexpr StringView kProjectManifestFile   = u8"Project.xml";
    inline constexpr StringView kProjectContentDir     = u8"Content";
    inline constexpr StringView kProjectSourcesDir     = u8"Sources";
    inline constexpr StringView kProjectCookedDir      = u8"Cooked";
    inline constexpr StringView kProjectEditorDir      = u8"Editor";
    inline constexpr StringView kProjectCacheDir       = u8".cache";
    inline constexpr StringView kSourceAssetExtension  = u8".xasset";   // readable/diffable envelopes
    inline constexpr StringView kCookedAssetExtension  = u8".rasset";   // binary envelopes

    // Shipped-dist layout (what the export CLI stages; the player detects dist by the pak).
    inline constexpr StringView kDistContentPak        = u8"Content.pak";
    inline constexpr StringView kDistManifestFile      = u8"player.xml";

    // The shared, committed part of a project (Project.xml payload) - also the dist manifest
    // (player.xml), which is the same shape minus editor-only concerns.
    class ProjectSettings final : public ISerializable
    {
        DRACONIC_OBJECT(ProjectSettings, ISerializable)
    public:
        String name;
        String engineVersion;  // engine that last saved this project (launcher/migration routing)
        String defaultScene;   // source-DB path of the startup scene ("" = none)
        String startupScript;  // project-relative GAME SCRIPT path ("" = none) - the scripted
                               // IApplication counterpart (launch/update/exit), hosted by the
                               // player and play-in-editor
        String nativeModule;   // RESERVED: optional native game module (tagged for later planning)

        // Migration branches on ar.Version() - the type's data version is written/read by
        // the manifest helpers below (DRACONIC_DEFINE_OBJECT_VERSIONED sets the current one).
        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "name", name);
            if (ar.Version() >= 2)   // v2 added the engine stamp
            {
                draconic::core::Serialize(ar, "engineVersion", engineVersion);
            }
            draconic::core::Serialize(ar, "defaultScene", defaultScene);
            draconic::core::Serialize(ar, "startupScript", startupScript);
            draconic::core::Serialize(ar, "nativeModule", nativeModule);
        }
    };

    /// Read a manifest (Project.xml / player.xml) from `root`. NotFound when absent.
    [[nodiscard]] inline Status LoadProjectSettings(vfs::IFileSystem& root, ProjectSettings& out,
                                                    StringView fileName = kProjectManifestFile)
    {
        UniquePtr<IStream> stream = root.Open(fileName, FileMode::Read);
        if (!stream) { return Status{ ErrorCode::NotFound }; }
        SerializerFactory factory = draconic::xml::XmlSerializerFactory();
        UniquePtr<SerializerContext> ctx = factory(*stream, SerializeMode::Read);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }
        BeginVersionedPayload(*ctx->serializer, ProjectSettings::StaticType());
        out.Serialize(*ctx->serializer);
        EndVersionedPayload(*ctx->serializer);
        return ctx->serializer->IsOk() ? Status{} : ctx->serializer->GetStatus();
    }

    /// Write a manifest to `root`.
    [[nodiscard]] inline Status SaveProjectSettings(vfs::IWritableFileSystem& writable,
                                                    ProjectSettings& settings,
                                                    StringView fileName = kProjectManifestFile)
    {
        settings.engineVersion = String(kEngineVersionString);   // every save re-stamps
        MemoryStream buffer;
        SerializerFactory factory = draconic::xml::XmlSerializerFactory();
        UniquePtr<SerializerContext> ctx = factory(buffer, SerializeMode::Write);
        if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }
        BeginVersionedPayload(*ctx->serializer, ProjectSettings::StaticType());
        settings.Serialize(*ctx->serializer);
        EndVersionedPayload(*ctx->serializer);
        if (!ctx->serializer->IsOk()) { return ctx->serializer->GetStatus(); }
        ctx->Flush(buffer);
        return writable.Save(fileName, buffer.Bytes());
    }

    DRACONIC_DEFINE_OBJECT_VERSIONED(ProjectSettings, "draconic::project", 2)
}
