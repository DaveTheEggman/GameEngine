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
import :export_preset;
import :export_template;

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
                               draconic::content::ContentDatabase& staging,
                               const HashMap<Guid, Array<byte>>* sceneStreams)
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

            // Scenes AND prefabs stage the same way (runtime scenes keep prefab instances as
            // ref+deltas, so the prefab payloads must ship in the pak for the load-time respawn).
            RefPtr<ISerializable> object = scene.ReadObject();
            draconic::content::Instance* staged = nullptr;
            if (auto* doc = Cast<draconic::scene::SceneDocument>(object.Get()))
            {
                staged = group->CreateInstanceWithId(
                    scene.Id(), scene.Name(), draconic::scene::SceneDocument::StaticType());
                if (staged == nullptr || !staged->WriteObject(*doc).IsOk()) { return false; }
            }
            else if (auto* prefab = Cast<draconic::scene::PrefabDocument>(object.Get()))
            {
                staged = group->CreateInstanceWithId(
                    scene.Id(), scene.Name(), draconic::scene::PrefabDocument::StaticType());
                if (staged == nullptr || !staged->WriteObject(*prefab).IsOk()) { return false; }
            }
            else { return false; }

            // Pre-transcoded BINARY stream (editor's main-thread pass) when available; else
            // the source stream verbatim - the runtime SNIFFS the encoding, so an XML source
            // staged as-is (headless CLI, no engine subsystems to transcode with) still
            // loads, just with text-parse cost.
            if (sceneStreams != nullptr)
            {
                if (const Array<byte>* pre = sceneStreams->Find(scene.Id()))
                {
                    return staged->WriteData(u8"scene",
                        Span<const byte>{ pre->Data(), pre->Size() }).IsOk();
                }
            }
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
                if (instance->TypeName() == u8"SceneDocument"
                    || instance->TypeName() == u8"PrefabDocument") { out.PushBack(instance); }
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

        // Copy srcDir/srcName -> dstDir/dstName, PRESERVING permissions (staged executables need the
        // +x bit, which a VFS read+write would drop). True on success.
        inline bool CopyFilePreserving(StringView srcDir, StringView srcName, StringView dstDir, StringView dstName)
        {
            // Core/System backend (POSIX re-applies the source mode; Windows CopyFile preserves
            // natively). std::filesystem is deliberately NOT used in this module: referencing it
            // from exported inline code broke GCC's module serialization for importers
            // ("failed to load pendings for __gnu_cxx::__concurrence_unlock_error").
            return FileCopyPreserving(PathJoin(srcDir, srcName).AsView(),
                                      PathJoin(dstDir, dstName).AsView());
        }

        // Last path component of `path` (after the final '/' or '\\').
        [[nodiscard]] inline StringView BaseName(StringView path)
        {
            usize start = 0;
            for (usize i = 0; i < path.Size(); ++i)
            {
                if (path[i] == utf8char('/') || path[i] == utf8char('\\')) { start = i + 1; }
            }
            return path.SubStr(start, path.Size() - start);
        }

        // A filesystem-safe output subdir from a preset name (alnum / - _ . kept, else '-').
        [[nodiscard]] inline String SanitizeName(StringView name)
        {
            String out;
            for (usize i = 0; i < name.Size(); ++i)
            {
                const utf8char c = name[i];
                const bool ok = (c >= utf8char('a') && c <= utf8char('z')) || (c >= utf8char('A') && c <= utf8char('Z'))
                             || (c >= utf8char('0') && c <= utf8char('9')) || c == utf8char('-')
                             || c == utf8char('_') || c == utf8char('.');
                out += ok ? StringView(&c, 1) : StringView(u8"-");
            }
            return out.IsEmpty() ? String(u8"export") : out;
        }
    }

    // A step/progress sink: `onProgress(stepLabel, fraction[0..1])`. Optional (the CLI passes none;
    // the editor's background job wires it to a JobContext for the status-bar progress bar).
    using ExportProgress = Function<void(StringView, f32)>;

    /// Stage scenes + pack Content.pak + write the dist manifest into `outDir`. Does NOT cook - it
    /// assumes the project's cooked dir is already up to date (the CLI's ExportProject cooks then calls
    /// this; the editor cooks via CookService first, then runs this on a background job). Fills
    /// stats.scenesStaged / stats.filesPacked.
    [[nodiscard]] inline Status ExportContent(EditorProject& project, StringView outDir,
                                              ExportStats& stats, const ExportProgress& onProgress = {},
                                              const HashMap<Guid, Array<byte>>* sceneStreams = nullptr)
    {
        namespace proj = draconic::project;

        // --- stage scenes ---
        if (onProgress) { onProgress(u8"Staging scenes...", 0.65f); }
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
                if (!detail::StageScene(*scene, staging, sceneStreams))
                {
                    DRACONIC_LOG_ERROR(u8"Export", u8"failed to stage scene '{}'", scene->Path());
                    return Status{ ErrorCode::Internal };
                }
            }
            stats.scenesStaged = scenes.Size();
        }

        // --- 3. pack ---
        if (onProgress) { onProgress(u8"Packing Content.pak...", 0.78f); }
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
        if (onProgress) { onProgress(u8"Writing manifest...", 0.9f); }
        {
            draconic::vfs::NativeFileSystem outMount(outDir);
            proj::ProjectSettings dist;
            dist.name = String(project.Settings().name.AsView());
            dist.defaultSceneId = project.Settings().defaultSceneId;
            dist.defaultScene = String(project.Settings().defaultScene.AsView());
            dist.startupScript = String(project.Settings().startupScript.AsView());
            if (!proj::SaveProjectSettings(*outMount.AsWritable(), dist, proj::kDistManifestFile).IsOk())
            {
                DRACONIC_LOG_ERROR(u8"Export", u8"failed to write the dist manifest");
                return Status{ ErrorCode::Internal };
            }
        }

        detail::RemoveTreeRecursive(stagingDir.AsView());
        return Status{};
    }

    /// Cook + ExportContent (the all-in-one; the CLI / one-shot path). The builder registry is the
    /// exe's full set (kept in lockstep across cook/editor/export). `rebuild` forces a clean cook.
    [[nodiscard]] inline Status ExportProject(EditorProject& project, StringView outDir,
                                              BuilderRegistry& builders, bool rebuild,
                                              ExportStats* outStats = nullptr,
                                              const ExportProgress& onProgress = {})
    {
        ExportStats stats;

        // --- cook ---
        if (onProgress) { onProgress(u8"Cooking content...", 0.05f); }
        draconic::vfs::NativeFileSystem sourcesMount(project.SourcesRoot().AsView());
        draconic::vfs::NativeFileSystem cacheMount(project.CacheRoot().AsView());
        JobSystem jobs;
        CookDriver driver(project.SourceDb(), project.CookedDb(), builders,
                          &sourcesMount, &cacheMount, &jobs);
        CookPlan plan = driver.Plan(rebuild);
        CookProgress cookProgress;
        cookProgress.onItem = [&onProgress](usize done, usize total, StringView path, bool)
        {
            if (!onProgress) { return; }
            const f32 frac = (total > 0) ? 0.05f + (static_cast<f32>(done) / static_cast<f32>(total)) * 0.55f
                                         : 0.6f;   // cook occupies 0.05..0.60 of the export
            String step(u8"Cooking "); step += path;
            onProgress(step.AsView(), frac);
        };
        const CookStats cookStats = driver.Execute(plan, &cookProgress);
        stats.cooked = cookStats.cooked;
        stats.cookFailed = cookStats.failed;
        if (cookStats.failed > 0)
        {
            DRACONIC_LOG_ERROR(u8"Export", u8"aborting - the cook has {} failure(s)", cookStats.failed);
            if (outStats != nullptr) { *outStats = stats; }
            return Status{ ErrorCode::Internal };
        }

        // --- content ---
        const Status s = ExportContent(project, outDir, stats, onProgress);
        if (outStats != nullptr) { *outStats = stats; }
        return s;
    }

    struct ExportResult
    {
        ExportStats content;         // cook/stage/pack totals
        usize filesStaged = 0;       // player + template sidecars + preset additionalFiles copied
        String outputDir;            // where the dist landed
        String engineVersionWarning; // set when the resolved template was built against a different
                                     // engine version (soft mismatch); empty otherwise. The export
                                     // still runs; callers may surface this to the user.
    };

    /// Produce ONE preset's dist under `outRoot`: resolve its template, export the content
    /// (ExportProject), then stage the template's player + sidecars and the preset's additionalFiles.
    /// The whole dist from one entry point - the CLI and the editor call this identically (the cook
    /// uniformity extended to the player, replacing the old exe-location walk in the CLI).
    // `cook` = false skips the cook and only packs/stages (ExportContent) - the editor uses this AFTER
    // cooking through its CookService (so the cook, which mutates the DB the UI reads, never runs on a
    // background job). The CLI leaves it true (cook + content in one shot).
    [[nodiscard]] inline Status ExportOne(EditorProject& project, const ExportPreset& preset,
                                          const TemplateRegistry& templates, BuilderRegistry& builders,
                                          StringView outRoot, bool rebuild, ExportResult* outResult = nullptr,
                                          const ExportProgress& onProgress = {}, bool cook = true,
                                          const HashMap<Guid, Array<byte>>* sceneStreams = nullptr)
    {
        const ExportTemplate* tmpl = templates.Resolve(preset);
        if (tmpl == nullptr)
        {
            DRACONIC_LOG_ERROR(u8"Export", u8"no export template for preset '{}' (platform '{}') - import one",
                               preset.name, preset.platform);
            return Status{ ErrorCode::NotFound };
        }

        ExportResult result;

        // Soft engine-version match (mirrors EditorProject::Open's project-manifest check): a template
        // built against a different engine version may be binary-incompatible with the cooked content,
        // but we don't know that it is - so warn and keep going rather than block. Empty version = an
        // older/hand-written template with no stamp; skip.
        if (!tmpl->engineVersion.IsEmpty()
            && tmpl->engineVersion != draconic::project::kEngineVersionString)
        {
            DRACONIC_LOG_WARNING(u8"Export",
                u8"template '{}' was built against engine {} but this build is {} - exporting anyway",
                tmpl->id, tmpl->engineVersion, draconic::project::kEngineVersionString);
            result.engineVersionWarning = String(u8"Template '");
            result.engineVersionWarning += tmpl->id;
            result.engineVersionWarning += u8"' targets engine ";
            result.engineVersionWarning += tmpl->engineVersion;
            result.engineVersionWarning += u8" (this build is ";
            result.engineVersionWarning += draconic::project::kEngineVersionString;
            result.engineVersionWarning += u8").";
        }

        const String subdir = preset.outputSubdir.IsEmpty() ? detail::SanitizeName(preset.name.AsView())
                                                            : String(preset.outputSubdir.AsView());
        result.outputDir = PathJoin(outRoot, subdir.AsView());

        // Ensure the output dir (and outRoot) exist before the content pipeline writes into it.
        (void)CreateDirectories(result.outputDir.AsView());

        const Status contentStatus = cook
            ? ExportProject(project, result.outputDir.AsView(), builders, rebuild, &result.content, onProgress)
            : ExportContent(project, result.outputDir.AsView(), result.content, onProgress,
                            sceneStreams);
        if (!contentStatus.IsOk())
        {
            if (outResult != nullptr) { *outResult = result; }
            return Status{ ErrorCode::Internal };
        }

        if (onProgress) { onProgress(u8"Staging player...", 0.93f); }
        // Player: <template dir>/<playerBinary> -> <outDir>/<preset.playerName | template.playerBinary>.
        const String outName = preset.playerName.IsEmpty() ? String(tmpl->playerBinary.AsView())
                                                          : String(preset.playerName.AsView());
        if (!detail::CopyFilePreserving(tmpl->directory.AsView(), tmpl->playerBinary.AsView(),
                                        result.outputDir.AsView(), outName.AsView()))
        {
            DRACONIC_LOG_ERROR(u8"Export", u8"failed to stage player '{}' from template '{}'",
                               tmpl->playerBinary, tmpl->id);
            if (outResult != nullptr) { *outResult = result; }
            return Status{ ErrorCode::Internal };
        }
        ++result.filesStaged;

        if (onProgress && !tmpl->sidecars.IsEmpty()) { onProgress(u8"Staging runtime libs...", 0.96f); }
        // Template sidecars (runtime libs) from the template dir.
        for (const String& sidecar : tmpl->sidecars)
        {
            if (detail::CopyFilePreserving(tmpl->directory.AsView(), sidecar.AsView(),
                                           result.outputDir.AsView(), sidecar.AsView()))
            {
                ++result.filesStaged;
            }
            else
            {
                DRACONIC_LOG_WARNING(u8"Export", u8"sidecar '{}' not found in template '{}'", sidecar, tmpl->id);
            }
        }

        // Preset additionalFiles (game extras, project-relative) -> <outDir>/<basename>.
        for (const String& extra : preset.additionalFiles)
        {
            const StringView base = detail::BaseName(extra.AsView());
            if (detail::CopyFilePreserving(project.Directory(), extra.AsView(), result.outputDir.AsView(), base))
            {
                ++result.filesStaged;
            }
            else
            {
                DRACONIC_LOG_WARNING(u8"Export", u8"additional file '{}' not found", extra);
            }
        }

        if (onProgress) { onProgress(u8"Done", 1.0f); }
        if (outResult != nullptr) { *outResult = result; }
        return Status{};
    }

    /// Produce EVERY preset's dist under `outRoot` (a failing preset is logged and skipped; the others
    /// continue). Returns Ok only when all presets succeeded.
    [[nodiscard]] inline Status ExportAll(EditorProject& project, Span<const ExportPreset> presets,
                                          const TemplateRegistry& templates, BuilderRegistry& builders,
                                          StringView outRoot, bool rebuild, const ExportProgress& onProgress = {},
                                          bool cook = true,
                                          const HashMap<Guid, Array<byte>>* sceneStreams = nullptr)
    {
        usize ok = 0;
        const usize n = presets.Size();
        for (usize i = 0; i < n; ++i)
        {
            const ExportPreset& preset = presets[i];
            // Scale each preset's 0..1 into its slice (i..i+1)/n and prefix its name.
            const ExportProgress scoped = [&onProgress, i, n, &preset](StringView step, f32 frac)
            {
                if (!onProgress) { return; }
                String s(preset.name.AsView()); s += u8": "; s += step;
                onProgress(s.AsView(), (static_cast<f32>(i) + frac) / static_cast<f32>(n));
            };
            ExportResult result;
            if (ExportOne(project, preset, templates, builders, outRoot, rebuild, &result, scoped, cook,
                          sceneStreams).IsOk())
            {
                ++ok;
                DRACONIC_LOG_INFO(u8"Export", u8"exported '{}' -> {} ({} files staged)",
                                  preset.name, result.outputDir, result.filesStaged);
            }
            else
            {
                DRACONIC_LOG_ERROR(u8"Export", u8"preset '{}' failed", preset.name);
            }
        }
        return (ok == n) ? Status{} : Status{ ErrorCode::Internal };
    }
}
