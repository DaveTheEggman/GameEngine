// Draconic::EditorCore - :project partition.
//
// The project model (docs/design/editor.md §3.9, decided 2026-07-11): a project is a
// self-contained directory with a fixed layout + an XML manifest:
//
//   <root>/Project.xml   shared manifest (committed) - ProjectSettings via the XML serializer
//   <root>/Content/      SOURCE content DB (XML factory, .xasset) - authored, committed
//   <root>/Sources/      raw import sources (.fbx/.png/...) referenced by Asset::fileName
//                        (this dir == AssetBuildContext.assetRoot) - committed
//   <root>/Cooked/       cooked content DB (binary factory, .rasset) - generated, gitignored
//   <root>/Editor/       per-user editor state (dock layout, open pages) - gitignored
//   <root>/.cache/       thumbnails + incremental-cook hash db - gitignored
//
// EditorProject::Open mounts Content/ + Cooked/ and opens both ContentDatabases (Traktor's
// source-db / output-db split). The manifest reserves `nativeModule` for the tagged-for-later
// optional per-project native game module (see design doc §5 deferred).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.editor.core:project;

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.xml.serialization;

using namespace draconic::core;

export namespace draconic::editor
{
    inline constexpr StringView kProjectManifestFile   = u8"Project.xml";
    inline constexpr StringView kProjectContentDir     = u8"Content";
    inline constexpr StringView kProjectSourcesDir     = u8"Sources";
    inline constexpr StringView kProjectCookedDir      = u8"Cooked";
    inline constexpr StringView kProjectEditorDir      = u8"Editor";
    inline constexpr StringView kProjectCacheDir       = u8".cache";
    inline constexpr StringView kSourceAssetExtension  = u8".xasset";   // readable/diffable envelopes
    inline constexpr StringView kCookedAssetExtension  = u8".rasset";   // binary envelopes

    // The shared, committed part of a project (Project.xml payload).
    class ProjectSettings final : public ISerializable
    {
        DRACONIC_OBJECT(ProjectSettings, ISerializable)
    public:
        String name;
        i32 formatVersion = 1;
        String defaultScene;   // source-DB path of the startup scene ("" = none)
        String nativeModule;   // RESERVED: optional native game module (tagged for later planning)

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "name", name);
            draconic::core::Serialize(ar, "formatVersion", formatVersion);
            draconic::core::Serialize(ar, "defaultScene", defaultScene);
            draconic::core::Serialize(ar, "nativeModule", nativeModule);
        }
    };

    // An opened project: the manifest + the mounted source and cooked content databases.
    class EditorProject
    {
    public:
        EditorProject(const EditorProject&) = delete;
        EditorProject& operator=(const EditorProject&) = delete;

        // Scaffold a new project at `directory` (created if absent; its PARENT must exist):
        // writes the manifest and creates the fixed subdirectories. Fails with AlreadyExists
        // if a manifest is already present.
        [[nodiscard]] static Status Create(StringView directory, StringView name)
        {
            if (!CreateDirectory(directory)) { return Status{ ErrorCode::NotFound }; }
            vfs::NativeFileSystem root(directory);
            if (root.Exists(kProjectManifestFile)) { return Status{ ErrorCode::AlreadyExists }; }

            const StringView dirs[] = { kProjectContentDir, kProjectSourcesDir, kProjectCookedDir,
                                        kProjectEditorDir, kProjectCacheDir };
            for (StringView dir : dirs)
            {
                if (!CreateDirectory(PathJoin(directory, dir).AsView())) { return Status{ ErrorCode::Internal }; }
            }

            ProjectSettings settings;
            settings.name = String(name);
            return WriteManifest(root, settings);
        }

        // Open an existing project: read the manifest, ensure the fixed subdirectories exist,
        // mount Content/ + Cooked/, and scan both content databases.
        [[nodiscard]] static UniquePtr<EditorProject> Open(StringView directory)
        {
            vfs::NativeFileSystem root(directory);
            UniquePtr<IStream> stream = root.Open(kProjectManifestFile, FileMode::Read);
            if (!stream) { return UniquePtr<EditorProject>{}; }

            SerializerFactory factory = draconic::xml::XmlSerializerFactory();
            UniquePtr<SerializerContext> ctx = factory(*stream, SerializeMode::Read);
            if (!ctx || ctx->serializer == nullptr) { return UniquePtr<EditorProject>{}; }

            ProjectSettings settings;
            settings.Serialize(*ctx->serializer);
            if (!ctx->serializer->IsOk()) { return UniquePtr<EditorProject>{}; }

            // Generated dirs may be missing on a fresh checkout (Cooked/Editor/.cache are
            // gitignored) - recreate them so mounts and state saves always have a target.
            const StringView dirs[] = { kProjectContentDir, kProjectSourcesDir, kProjectCookedDir,
                                        kProjectEditorDir, kProjectCacheDir };
            for (StringView dir : dirs)
            {
                if (!CreateDirectory(PathJoin(directory, dir).AsView())) { return UniquePtr<EditorProject>{}; }
            }

            EditorProject* project = DefaultAllocator().New<EditorProject>(directory, settings);
            return UniquePtr<EditorProject>(project, DefaultAllocator());
        }

        [[nodiscard]] StringView Name() const noexcept { return m_settings.name.AsView(); }
        [[nodiscard]] StringView Directory() const noexcept { return m_directory.AsView(); }
        [[nodiscard]] ProjectSettings& Settings() noexcept { return m_settings; }
        [[nodiscard]] const ProjectSettings& Settings() const noexcept { return m_settings; }

        /// The authored source database (XML envelopes) - what the editor edits.
        [[nodiscard]] draconic::content::ContentDatabase& SourceDb() noexcept { return *m_sourceDb; }
        /// The cooked output database (binary envelopes) - what the runtime loads.
        [[nodiscard]] draconic::content::ContentDatabase& CookedDb() noexcept { return *m_cookedDb; }

        /// Root for raw import sources (== AssetBuildContext.assetRoot).
        [[nodiscard]] String SourcesRoot() const { return PathJoin(m_directory.AsView(), kProjectSourcesDir); }
        /// Per-user editor state directory (dock layout, open pages).
        [[nodiscard]] String EditorStateRoot() const { return PathJoin(m_directory.AsView(), kProjectEditorDir); }
        /// Cache directory (thumbnails, cook hashes).
        [[nodiscard]] String CacheRoot() const { return PathJoin(m_directory.AsView(), kProjectCacheDir); }

        // Persist the manifest (settings changed in the editor).
        [[nodiscard]] Status SaveSettings()
        {
            vfs::NativeFileSystem root(m_directory.AsView());
            return WriteManifest(root, m_settings);
        }

        // Internal (public for allocator New); use Create/Open. Fields are moved out of
        // `settings` one by one (ISerializable's deleted copy suppresses the implicit move).
        EditorProject(StringView directory, ProjectSettings& settings)
            : m_directory(directory)
            , m_contentMount(MakeUnique<vfs::NativeFileSystem>(DefaultAllocator(),
                  PathJoin(directory, kProjectContentDir).AsView()))
            , m_cookedMount(MakeUnique<vfs::NativeFileSystem>(DefaultAllocator(),
                  PathJoin(directory, kProjectCookedDir).AsView()))
            , m_sourceDb(MakeUnique<draconic::content::ContentDatabase>(DefaultAllocator(),
                  *m_contentMount, draconic::xml::XmlSerializerFactory(), kSourceAssetExtension))
            , m_cookedDb(MakeUnique<draconic::content::ContentDatabase>(DefaultAllocator(),
                  *m_cookedMount, BinarySerializerFactory(), kCookedAssetExtension))
        {
            m_settings.name          = Move(settings.name);
            m_settings.formatVersion = settings.formatVersion;
            m_settings.defaultScene  = Move(settings.defaultScene);
            m_settings.nativeModule  = Move(settings.nativeModule);
        }

    private:
        [[nodiscard]] static Status WriteManifest(vfs::NativeFileSystem& root, ProjectSettings& settings)
        {
            vfs::IWritableFileSystem* writable = root.AsWritable();
            if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }

            MemoryStream buffer;
            SerializerFactory factory = draconic::xml::XmlSerializerFactory();
            UniquePtr<SerializerContext> ctx = factory(buffer, SerializeMode::Write);
            if (!ctx || ctx->serializer == nullptr) { return Status{ ErrorCode::Internal }; }

            settings.Serialize(*ctx->serializer);
            if (!ctx->serializer->IsOk()) { return ctx->serializer->GetStatus(); }
            ctx->Flush(buffer);

            return writable->Save(kProjectManifestFile, buffer.Bytes());
        }

        String m_directory;
        ProjectSettings m_settings;
        UniquePtr<vfs::NativeFileSystem> m_contentMount;
        UniquePtr<vfs::NativeFileSystem> m_cookedMount;
        UniquePtr<draconic::content::ContentDatabase> m_sourceDb;
        UniquePtr<draconic::content::ContentDatabase> m_cookedDb;
    };

    DRACONIC_DEFINE_OBJECT(ProjectSettings, "draconic::editor")
}
