// Draconic::EditorCore - :export partition.
//
// The export pipeline as a LIBRARY (the RaptorExport CLI and the editor's Export menu are
// thin callers; tests drive it headlessly): cook -> stage scenes as binary envelopes ->
// pack Content.pak -> write the dist manifest. The caller stages the player executable
// (an exe-location concern, not a pipeline one).
//
//   dist layout:  <out>/Content.pak   products + scenes (one binary DB) + game script (raw)
//                 <out>/player.xml    dist manifest (ProjectSettings shape)
//
// Scenes are builder-less by design (their cooked form IS the authored form), so export
// re-encodes each XML envelope to binary and copies the "scene" stream - SAME guids, so
// resource refs and the manifest's defaultScene path keep working in the pak.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.core:export_pipeline;

import draconic.core;
import draconic.vfs;
import draconic.vfs.pak;
import draconic.content;
import draconic.project;
import draconic.scene.resource;
import draconic.editor;
import draconic.editor.cook;
import :project;

using namespace draconic::core;

export namespace draconic::editor
{
    struct ExportStats
    {
        usize cooked = 0;
        usize cookFailed = 0;
        usize scenesStaged = 0;
        usize filesPacked = 0;
    };

    namespace detail
    {
        // Recursively add every file under `folder` to the pak (locator = mount-relative path).
        inline bool PackTree(draconic::vfs::IFileSystem& mount,
                             draconic::vfs::IEnumerableFileSystem& enumerable,
                             StringView folder, draconic::vfs::PakBuilder& pak, usize& fileCount)
        {
            Array<draconic::vfs::DirEntry> entries;
            if (!enumerable.Enumerate(folder, entries).IsOk()) { return folder.IsEmpty(); }
            for (const draconic::vfs::DirEntry& entry : entries)
            {
                const String path = PathJoin(folder, entry.name.AsView());
                if (entry.isDirectory)
                {
                    if (!PackTree(mount, enumerable, path.AsView(), pak, fileCount)) { return false; }
                    continue;
                }
                UniquePtr<IStream> stream = mount.Open(path.AsView(), FileMode::Read);
                if (!stream) { return false; }
                Array<byte> bytes;
                bytes.Resize(static_cast<usize>(stream->Size()));
                if (stream->Read(bytes.Data(), bytes.Size()) != bytes.Size()) { return false; }
                pak.Add(path.AsView(), Span<const byte>{ bytes.Data(), bytes.Size() });
                ++fileCount;
            }
            return true;
        }

        // Re-encode a scene instance into a binary envelope in the staging DB - SAME guid/path.
        inline bool StageScene(draconic::content::Instance& scene,
                               draconic::content::ContentDatabase& staging)
        {
            draconic::content::Group* group = staging.RootGroup();
            const String path = scene.OwningGroup().Path();
            usize start = 0;
            const StringView folder = path.AsView();
            for (usize i = 0; i <= folder.Size(); ++i)
            {
                if (i == folder.Size() || folder[i] == utf8char('/'))
                {
                    if (i > start)
                    {
                        group = group->CreateGroup(folder.SubStr(start, i - start));
                        if (group == nullptr) { return false; }
                    }
                    start = i + 1;
                }
            }

            RefPtr<ISerializable> object = scene.ReadObject();
            auto* doc = Cast<draconic::scene::SceneDocument>(object.Get());
            if (doc == nullptr) { return false; }
            draconic::content::Instance* staged = group->CreateInstanceWithId(
                scene.Id(), scene.Name(), draconic::scene::SceneDocument::StaticType());
            if (staged == nullptr || !staged->WriteObject(*doc).IsOk()) { return false; }

            if (UniquePtr<IStream> stream = scene.ReadData(u8"scene"))
            {
                Array<byte> bytes;
                bytes.Resize(static_cast<usize>(stream->Size()));
                if (stream->Read(bytes.Data(), bytes.Size()) != bytes.Size()) { return false; }
                if (!staged->WriteData(u8"scene", Span<const byte>{ bytes.Data(), bytes.Size() }).IsOk())
                {
                    return false;
                }
            }
            return true;
        }

        inline void CollectScenes(draconic::content::Group& group,
                                  Array<draconic::content::Instance*>& out)
        {
            for (draconic::content::Instance* instance : group.Instances())
            {
                if (instance->TypeName() == u8"SceneDocument") { out.PushBack(instance); }
            }
            for (draconic::content::Group* child : group.Groups()) { CollectScenes(*child, out); }
        }

        inline void RemoveTreeRecursive(StringView root)
        {
            draconic::vfs::NativeFileSystem fs(root);
            Array<draconic::vfs::DirEntry> entries;
            if (fs.AsEnumerable()->Enumerate(u8"", entries).IsOk())
            {
                for (const draconic::vfs::DirEntry& entry : entries)
                {
                    if (entry.isDirectory)
                    {
                        RemoveTreeRecursive(PathJoin(root, entry.name.AsView()).AsView());
                    }
                    else { (void)fs.AsWritable()->Delete(entry.name.AsView()); }
                }
            }
            (void)RemoveDirectory(root);
        }
    }

    /// Cook + stage + pack `project` into `outDir`. The builder registry is the exe's full
    /// set (kept in lockstep across cook/editor/export). `rebuild` forces a clean cook.
    [[nodiscard]] inline Status ExportProject(EditorProject& project, StringView outDir,
                                              BuilderRegistry& builders, bool rebuild,
                                              ExportStats* outStats = nullptr)
    {
        namespace proj = draconic::project;
        ExportStats stats;

        // --- 1. cook ---
        draconic::vfs::NativeFileSystem sourcesMount(project.SourcesRoot().AsView());
        draconic::vfs::NativeFileSystem cacheMount(project.CacheRoot().AsView());
        JobSystem jobs;
        CookDriver driver(project.SourceDb(), project.CookedDb(), builders,
                          &sourcesMount, &cacheMount, &jobs);
        CookPlan plan = driver.Plan(rebuild);
        const CookStats cookStats = driver.Execute(plan, nullptr);
        stats.cooked = cookStats.cooked;
        stats.cookFailed = cookStats.failed;
        if (cookStats.failed > 0)
        {
            DRACONIC_LOG_ERROR(u8"Export", u8"aborting - the cook has {} failure(s)", cookStats.failed);
            if (outStats != nullptr) { *outStats = stats; }
            return Status{ ErrorCode::Internal };
        }

        // --- 2. stage scenes ---
        if (!CreateDirectory(outDir)) { return Status{ ErrorCode::NotSupported }; }
        const String stagingDir = PathJoin(outDir, u8".stage-scenes");
        (void)CreateDirectory(stagingDir.AsView());
        draconic::vfs::NativeFileSystem stagingMount(stagingDir.AsView());
        {
            draconic::content::ContentDatabase staging(stagingMount, BinarySerializerFactory(),
                                                       proj::kCookedAssetExtension);
            Array<draconic::content::Instance*> scenes;
            detail::CollectScenes(*project.SourceDb().RootGroup(), scenes);
            for (draconic::content::Instance* scene : scenes)
            {
                if (!detail::StageScene(*scene, staging))
                {
                    DRACONIC_LOG_ERROR(u8"Export", u8"failed to stage scene '{}'", scene->Path());
                    return Status{ ErrorCode::Internal };
                }
            }
            stats.scenesStaged = scenes.Size();
        }

        // --- 3. pack ---
        draconic::vfs::PakBuilder pak;
        draconic::vfs::NativeFileSystem cookedMount(
            PathJoin(project.Directory(), proj::kProjectCookedDir).AsView());
        if (!detail::PackTree(cookedMount, *cookedMount.AsEnumerable(), u8"", pak, stats.filesPacked)
            || !detail::PackTree(stagingMount, *stagingMount.AsEnumerable(), u8"", pak, stats.filesPacked))
        {
            DRACONIC_LOG_ERROR(u8"Export", u8"packing failed");
            return Status{ ErrorCode::Internal };
        }
        if (!project.Settings().startupScript.IsEmpty())
        {
            draconic::vfs::NativeFileSystem projectRoot(project.Directory());
            const StringView scriptPath = project.Settings().startupScript.AsView();
            if (UniquePtr<IStream> stream = projectRoot.Open(scriptPath, FileMode::Read))
            {
                Array<byte> bytes;
                bytes.Resize(static_cast<usize>(stream->Size()));
                if (stream->Read(bytes.Data(), bytes.Size()) == bytes.Size())
                {
                    pak.Add(scriptPath, Span<const byte>{ bytes.Data(), bytes.Size() });
                    ++stats.filesPacked;
                }
            }
            else
            {
                DRACONIC_LOG_WARNING(u8"Export", u8"startup script '{}' not found", scriptPath);
            }
        }
        const String pakPath = PathJoin(outDir, proj::kDistContentPak);
        if (!pak.Write(pakPath.AsView()).IsOk())
        {
            DRACONIC_LOG_ERROR(u8"Export", u8"failed to write Content.pak");
            return Status{ ErrorCode::Internal };
        }

        // --- 4. dist manifest ---
        {
            draconic::vfs::NativeFileSystem outMount(outDir);
            proj::ProjectSettings dist;
            dist.name = String(project.Settings().name.AsView());
            dist.defaultScene = String(project.Settings().defaultScene.AsView());
            dist.startupScript = String(project.Settings().startupScript.AsView());
            if (!proj::SaveProjectSettings(*outMount.AsWritable(), dist, proj::kDistManifestFile).IsOk())
            {
                DRACONIC_LOG_ERROR(u8"Export", u8"failed to write the dist manifest");
                return Status{ ErrorCode::Internal };
            }
        }

        detail::RemoveTreeRecursive(stagingDir.AsView());
        if (outStats != nullptr) { *outStats = stats; }
        return Status{};
    }
}
