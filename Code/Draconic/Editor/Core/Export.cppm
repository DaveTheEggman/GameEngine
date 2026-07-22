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
import :export_roots;
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

    // === Reachability pruning (docs/design/export-reachability.md, Phase 1) ===
    //
    // Opt-in per preset (ExportPreset::pruneToReachable): a dist ships only the CLOSURE of its
    // entry points instead of the whole cooked dir. The closure reuses the cook's dependency graph
    // (CookDriver::PlanFor) for asset->asset edges; a caller-supplied SceneReferenceScanner bridges
    // the scene-graph edges the cook doesn't model (a scene's component resource Refs + its prefab
    // instances), because scenes/prefabs are builder-less and thus outside PlanFor's read-dep walk.

    // Why a root is in the dist. Phase 1 seeds DefaultScene + StartupScript; Phase 2 ("Always
    // Export") adds Flag / Group; Phase 4 adds ScriptLiteral. Keep the enum stable for the report.
    enum class ExportRootReason
    {
        DefaultScene,   // ProjectSettings::defaultSceneId
        StartupScript,  // the startup script's own imported asset (the script FILE ships regardless)
        Flag,           // an instance flagged "Always Export" (ExportRoots::instances)
        Group,          // an instance under a group flagged "Always export contents" (ExportRoots::groups)
    };

    [[nodiscard]] inline StringView ExportRootReasonName(ExportRootReason r)
    {
        switch (r)
        {
            case ExportRootReason::DefaultScene:  return u8"default-scene";
            case ExportRootReason::StartupScript: return u8"startup-script";
            case ExportRootReason::Flag:          return u8"always-export";
            case ExportRootReason::Group:         return u8"always-export-group";
        }
        return u8"?";
    }

    // A seed entry point: the dist is the closure of these. The Array<ExportRoot> the seeding
    // function returns is the SEAM Phase 2 extends (it just appends Flag/Group roots).
    struct ExportRoot
    {
        Guid id;
        String name;                 // the instance's source path (report display); "" if unresolved
        ExportRootReason reason = ExportRootReason::DefaultScene;
    };

    // What a scan of one scene/prefab instance yields: the guids it references directly. Resources
    // feed PlanFor (which then closes asset->asset); prefabs are staged AND rescanned for their own
    // references (the scene->prefab->asset chain).
    struct SceneReferences
    {
        Array<Guid> resources;   // component resource Ref ids (mesh/material/texture/... instances)
        Array<Guid> prefabs;     // prefab-instance ids nested in this scene/prefab
    };

    // Caller hook: collect one scene/prefab instance's direct references (see SceneReferences).
    // The export LIBRARY stays subsystem-agnostic, so the driving tool - which owns the full
    // component-manager set (render/physics/animation/...) - supplies this. The canonical
    // implementation loads the instance (LoadScene over all managers), resolves its Refs through a
    // factory-less ResourceManager and reads back ResourceManager::CollectUnresolved (every bound
    // id, since nothing built), plus each parked prefab instance's prefabId. Same reason the
    // scene-stream transcode (sceneStreams) is a caller hook.
    using SceneReferenceScanner =
        Function<void(draconic::content::Instance&, draconic::content::ContentDatabase&, SceneReferences&)>;

    // The loud, auditable record of a pruned export: which roots were kept and WHY, plus what was
    // dropped. Lives on ExportResult (CLI prints it, editor Console shows it) and is written beside
    // the dist as export-report.txt. Empty/pruned=false for a pack-everything export.
    struct PruningReport
    {
        bool pruned = false;
        Array<ExportRoot> roots;     // the seed entry points + their reasons
        usize keptCount = 0;         // scenes + cooked products shipped (closure size on disk)
        Array<String> dropped;       // instance paths excluded from the dist (WIP/unreferenced)
    };

    namespace detail
    {
        // A cooked file's owning instance is reachable: the owning instance path is `file` up to a
        // '.' in its NAME region (envelope "<path>.<ext>" and stream "<path>.<stream>.bin" both begin
        // with "<path>."). Tests each '.' boundary against the reachable-instance-path set; the '.'
        // delimiter makes prefix matching collision-safe (a peer "CubeBig" never matches "Cube.").
        [[nodiscard]] inline bool FileOwnerReachable(StringView file,
                                                     const HashMap<String, u8>& reachablePaths)
        {
            usize nameStart = 0;
            for (usize i = 0; i < file.Size(); ++i)
            {
                if (file[i] == utf8char('/')) { nameStart = i + 1; }
            }
            for (usize i = nameStart; i < file.Size(); ++i)
            {
                if (file[i] == utf8char('.'))
                {
                    if (reachablePaths.Find(String(file.SubStr(0, i))) != nullptr) { return true; }
                }
            }
            return false;
        }

        // Recursively add every file under `folder` to the pak (locator = mount-relative path).
        // When `reachablePaths` is non-null, packs ONLY files whose owning instance path is in the
        // set (closure pruning); null packs the whole tree (the default "export everything").
        inline bool PackTree(draconic::vfs::IFileSystem& mount,
                             draconic::vfs::IEnumerableFileSystem& enumerable,
                             StringView folder, draconic::vfs::PakBuilder& pak, usize& fileCount,
                             const HashMap<String, u8>* reachablePaths = nullptr)
        {
            Array<draconic::vfs::DirEntry> entries;
            if (!enumerable.Enumerate(folder, entries).IsOk()) { return folder.IsEmpty(); }
            for (const draconic::vfs::DirEntry& entry : entries)
            {
                const String path = PathJoin(folder, entry.name.AsView());
                if (entry.isDirectory)
                {
                    if (!PackTree(mount, enumerable, path.AsView(), pak, fileCount, reachablePaths)) { return false; }
                    continue;
                }
                if (reachablePaths != nullptr && !FileOwnerReachable(path.AsView(), *reachablePaths))
                {
                    continue;   // pruned: not part of the reachable closure
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

        // Every instance in a database (used to enumerate cooked products for the dropped report).
        inline void CollectAllInstances(draconic::content::Group& group,
                                        Array<draconic::content::Instance*>& out)
        {
            for (draconic::content::Instance* instance : group.Instances()) { out.PushBack(instance); }
            for (draconic::content::Group* child : group.Groups()) { CollectAllInstances(*child, out); }
        }

        // The source instance whose asset imports `fileName` (the startup script's own asset, if the
        // project imported the script). Nil when none - the script FILE still ships either way.
        [[nodiscard]] inline Guid FindAssetByFileName(draconic::content::ContentDatabase& db,
                                                      StringView fileName)
        {
            Array<draconic::content::Instance*> instances;
            CollectAllInstances(*db.RootGroup(), instances);
            for (draconic::content::Instance* instance : instances)
            {
                RefPtr<ISerializable> object = instance->ReadObject();
                if (const Asset* asset = Cast<Asset>(object.Get()))
                {
                    if (!asset->fileName.IsEmpty() && asset->fileName.AsView() == fileName)
                    {
                        return instance->Id();
                    }
                }
            }
            return Guid{};
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

    // === Reachability pruning helpers ===

    /// Seed export roots: the entry points whose closure the dist ships. Phase 1 = defaultSceneId
    /// (+ the startup script's own imported asset, if any); Phase 2 = the project's explicit
    /// "Always Export" set (ExportRoots) - flagged instances (Flag) and every instance under a
    /// flagged group subtree (Group). Deduped by guid, keeping the FIRST (highest-priority) reason,
    /// so the pruning report lists each root once with a stable why.
    [[nodiscard]] inline Array<ExportRoot> CollectExportRoots(EditorProject& project)
    {
        Array<ExportRoot> roots;
        HashMap<Guid, u8> seen;
        const auto add = [&](const Guid& id, ExportRootReason reason)
        {
            if (id.IsNil() || seen.Find(id) != nullptr) { return; }
            seen.InsertOrAssign(id, u8(1));
            ExportRoot root;
            root.id = id;
            root.reason = reason;
            if (draconic::content::Instance* inst = project.SourceDb().GetInstance(id))
            {
                root.name = inst->Path();
            }
            roots.PushBack(Move(root));
        };

        const draconic::project::ProjectSettings& settings = project.Settings();

        add(settings.defaultSceneId, ExportRootReason::DefaultScene);

        // The startup game script is a cooked ScriptClass asset (guid-authoritative) - seed it as a
        // reachability root so it (and the assets IT loads, via the normal AssetRef contract) ship.
        add(settings.startupScriptId, ExportRootReason::StartupScript);

        // Phase 2 "Always Export": explicit instance flags, then group subtrees (dynamic membership -
        // whatever is under the flagged folder now). A group that also contains the default scene /
        // a directly-flagged instance is deduped above, keeping the earlier reason.
        const ExportRootsSet& always = project.ExportRoots();
        for (const Guid& id : always.instances) { add(id, ExportRootReason::Flag); }
        for (const String& groupPath : always.groups)
        {
            Array<Guid> members;
            CollectGroupInstances(project.SourceDb(), groupPath.AsView(), members);
            for (const Guid& id : members) { add(id, ExportRootReason::Group); }
        }
        return roots;
    }

    /// Expand seed roots across the scene-graph edges the cook does not model: scan each scene/prefab
    /// root for its component resource Refs (feed PlanFor) and its prefab instances (staged AND
    /// rescanned), transitively. Returns the deduped guid set to seed CookDriver::PlanFor with -
    /// PlanFor then closes the asset->asset edges. `scanner` bridges scene->asset; without it the
    /// scene contents can't be discovered (caller must supply it when pruning).
    [[nodiscard]] inline Array<Guid> ExpandReachableRoots(EditorProject& project,
                                                          const Array<ExportRoot>& seeds,
                                                          const SceneReferenceScanner& scanner)
    {
        Array<Guid> out;
        HashMap<Guid, u8> seen;
        Array<Guid> queue;
        const auto push = [&](const Guid& id)
        {
            if (id.IsNil() || seen.Find(id) != nullptr) { return; }
            seen.InsertOrAssign(id, u8(1));
            out.PushBack(id);
            queue.PushBack(id);
        };
        for (const ExportRoot& r : seeds) { push(r.id); }

        usize head = 0;
        while (head < queue.Size())
        {
            const Guid id = queue[head++];
            draconic::content::Instance* inst = project.SourceDb().GetInstance(id);
            if (inst == nullptr) { continue; }
            const bool isSceneLike = inst->TypeName() == u8"SceneDocument"
                                  || inst->TypeName() == u8"PrefabDocument";
            if (!isSceneLike || !scanner) { continue; }

            SceneReferences refs;
            scanner(*inst, project.SourceDb(), refs);
            for (const Guid& g : refs.resources) { push(g); }
            for (const Guid& g : refs.prefabs) { push(g); }
        }
        return out;
    }

    /// Cook + compute the reachable closure with ONE CookDriver: PlanFor(planRoots) yields the
    /// closure (asset->asset), and - when `cook` - Execute cooks ONLY that set (cook only what
    /// ships). Fills stats.cooked / stats.cookFailed and `outReachable` (the full closure guids).
    [[nodiscard]] inline Status CookReachable(EditorProject& project, BuilderRegistry& builders,
                                              Span<const Guid> planRoots, bool cook, bool rebuild,
                                              ExportStats& stats, Array<Guid>& outReachable,
                                              const ExportProgress& onProgress = {})
    {
        draconic::vfs::NativeFileSystem sourcesMount(project.SourcesRoot().AsView());
        draconic::vfs::NativeFileSystem cacheMount(project.CacheRoot().AsView());
        JobSystem jobs;
        CookDriver driver(project.SourceDb(), project.CookedDb(), builders,
                          &sourcesMount, &cacheMount, &jobs);
        CookPlan plan = driver.PlanFor(planRoots, rebuild);
        outReachable = plan.reachable;

        if (cook)
        {
            CookProgress cookProgress;
            cookProgress.onItem = [&onProgress](usize done, usize total, StringView path, bool)
            {
                if (!onProgress) { return; }
                const f32 frac = (total > 0) ? 0.05f + (static_cast<f32>(done) / static_cast<f32>(total)) * 0.55f
                                             : 0.6f;
                String step(u8"Cooking "); step += path;
                onProgress(step.AsView(), frac);
            };
            const CookStats cookStats = driver.Execute(plan, &cookProgress);
            stats.cooked = cookStats.cooked;
            stats.cookFailed = cookStats.failed;
            if (cookStats.failed > 0)
            {
                DRACONIC_LOG_ERROR(u8"Export", u8"aborting - the cook has {} failure(s)", cookStats.failed);
                return Status{ ErrorCode::Internal };
            }
        }
        return Status{};
    }

    /// Render a pruning report as human-readable text (the on-disk export-report.txt + Console dump).
    [[nodiscard]] inline String FormatPruningReport(const PruningReport& report)
    {
        String out(u8"Export reachability pruning report\n");
        out += u8"==================================\n";
        out += Format(u8"kept: {} instance(s) in the closure\n", report.keptCount);
        out += Format(u8"roots: {}\n", report.roots.Size());
        for (const ExportRoot& r : report.roots)
        {
            out += u8"  - ";
            out += r.name.IsEmpty() ? StringView(u8"(unresolved)") : r.name.AsView();
            out += u8"  [";
            out += ExportRootReasonName(r.reason);
            out += u8"]\n";
        }
        out += Format(u8"dropped: {} instance(s)\n", report.dropped.Size());
        for (const String& d : report.dropped)
        {
            out += u8"  - ";
            out += d.AsView();
            out += u8"\n";
        }
        return out;
    }

    /// Stage scenes + pack Content.pak + write the dist manifest into `outDir`. Does NOT cook - it
    /// assumes the project's cooked dir is already up to date (the CLI's ExportProject cooks then calls
    /// this; the editor cooks via CookService first, then runs this on a background job). Fills
    /// stats.scenesStaged / stats.filesPacked.
    // `reachable` (opt-in closure pruning): when non-null, stage ONLY reachable scenes and pack ONLY
    // cooked products whose source guid is in the set - the whole cooked dir + every scene otherwise.
    // `roots` + `outReport` feed the pruning report (kept roots + reasons, dropped list), written
    // beside the dist as export-report.txt and returned to the caller. All null => today's behavior,
    // byte-for-byte.
    [[nodiscard]] inline Status ExportContent(EditorProject& project, StringView outDir,
                                              ExportStats& stats, const ExportProgress& onProgress = {},
                                              const HashMap<Guid, Array<byte>>* sceneStreams = nullptr,
                                              const HashMap<Guid, u8>* reachable = nullptr,
                                              const Array<ExportRoot>* roots = nullptr,
                                              PruningReport* outReport = nullptr)
    {
        namespace proj = draconic::project;

        // --- stage scenes ---
        if (onProgress) { onProgress(u8"Staging scenes...", 0.65f); }
        if (!CreateDirectory(outDir)) { return Status{ ErrorCode::NotSupported }; }
        const String stagingDir = PathJoin(outDir, u8".stage-scenes");
        (void)CreateDirectory(stagingDir.AsView());
        draconic::vfs::NativeFileSystem stagingMount(stagingDir.AsView());
        Array<String> droppedScenes;   // for the pruning report
        {
            draconic::content::ContentDatabase staging(stagingMount, BinarySerializerFactory(),
                                                       proj::kCookedAssetExtension);
            Array<draconic::content::Instance*> scenes;
            detail::CollectScenes(*project.SourceDb().RootGroup(), scenes);
            usize staged = 0;
            for (draconic::content::Instance* scene : scenes)
            {
                if (reachable != nullptr && reachable->Find(scene->Id()) == nullptr)
                {
                    droppedScenes.PushBack(scene->Path());   // pruned: not reachable from any root
                    continue;
                }
                if (!detail::StageScene(*scene, staging, sceneStreams))
                {
                    DRACONIC_LOG_ERROR(u8"Export", u8"failed to stage scene '{}'", scene->Path());
                    return Status{ ErrorCode::Internal };
                }
                ++staged;
            }
            stats.scenesStaged = staged;
        }

        // --- 3. pack ---
        // When pruning, resolve the reachable guids to their COOKED instance paths so the pack walk
        // can filter cooked files (a file belongs to instance P iff it begins "P.").
        UniquePtr<HashMap<String, u8>> reachablePaths;
        usize keptProducts = 0;
        Array<String> droppedProducts;
        if (reachable != nullptr)
        {
            reachablePaths = MakeUnique<HashMap<String, u8>>(DefaultAllocator());
            Array<draconic::content::Instance*> cooked;
            detail::CollectAllInstances(*project.CookedDb().RootGroup(), cooked);
            for (draconic::content::Instance* product : cooked)
            {
                if (reachable->Find(product->Id()) != nullptr)
                {
                    reachablePaths->InsertOrAssign(product->Path(), u8(1));
                    ++keptProducts;
                }
            }
            // Dropped = the authored (non-scene) source assets excluded from the dist. Computed from
            // the SOURCE db, not the cooked db: a scoped cook never PRODUCES the unreachable assets,
            // so they wouldn't appear cooked - but they're exactly what pruning left out, so the
            // report must name them (scenes are reported via droppedScenes above).
            Array<draconic::content::Instance*> sources;
            detail::CollectAllInstances(*project.SourceDb().RootGroup(), sources);
            for (draconic::content::Instance* src : sources)
            {
                const bool isSceneLike = src->TypeName() == u8"SceneDocument"
                                      || src->TypeName() == u8"PrefabDocument";
                if (!isSceneLike && reachable->Find(src->Id()) == nullptr)
                {
                    droppedProducts.PushBack(src->Path());
                }
            }
        }

        if (onProgress) { onProgress(u8"Packing Content.pak...", 0.78f); }
        draconic::vfs::PakBuilder pak;
        draconic::vfs::NativeFileSystem cookedMount(
            PathJoin(project.Directory(), proj::kProjectCookedDir).AsView());
        if (!detail::PackTree(cookedMount, *cookedMount.AsEnumerable(), u8"", pak, stats.filesPacked,
                              reachablePaths.Get())
            || !detail::PackTree(stagingMount, *stagingMount.AsEnumerable(), u8"", pak, stats.filesPacked))
        {
            DRACONIC_LOG_ERROR(u8"Export", u8"packing failed");
            return Status{ ErrorCode::Internal };
        }
        // The startup game script needs no special staging - it is a cooked ScriptClass asset in the
        // reachability closure, so it already rides in the content DB pak like every other asset. The
        // dist manifest carries its guid (below); the player binds it from the content DB.
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
            dist.startupScriptId = project.Settings().startupScriptId;
            dist.startupScript = String(project.Settings().startupScript.AsView());   // display mirror
            if (!proj::SaveProjectSettings(*outMount.AsWritable(), dist, proj::kDistManifestFile).IsOk())
            {
                DRACONIC_LOG_ERROR(u8"Export", u8"failed to write the dist manifest");
                return Status{ ErrorCode::Internal };
            }
        }

        // --- 5. pruning report (loud + auditable: pruning can silently break a shipped game) ---
        if (reachable != nullptr)
        {
            PruningReport report;
            report.pruned = true;
            if (roots != nullptr) { report.roots = *roots; }
            report.keptCount = stats.scenesStaged + keptProducts;
            for (const String& d : droppedScenes) { report.dropped.PushBack(String(d.AsView())); }
            for (const String& d : droppedProducts) { report.dropped.PushBack(String(d.AsView())); }

            const String text = FormatPruningReport(report);
            DRACONIC_LOG_INFO(u8"Export", u8"pruned dist: {} kept, {} dropped ({} root(s))",
                              report.keptCount, report.dropped.Size(), report.roots.Size());
            {
                draconic::vfs::NativeFileSystem outMount(outDir);
                (void)outMount.AsWritable()->Save(u8"export-report.txt", Span<const byte>(
                    reinterpret_cast<const byte*>(text.CStr()), text.Size()));
            }
            if (outReport != nullptr) { *outReport = Move(report); }
        }

        detail::RemoveTreeRecursive(stagingDir.AsView());
        return Status{};
    }

    /// Cook + ExportContent (the all-in-one; the CLI / one-shot path). The builder registry is the
    /// exe's full set (kept in lockstep across cook/editor/export). `rebuild` forces a clean cook.
    [[nodiscard]] inline Status ExportProject(EditorProject& project, StringView outDir,
                                              BuilderRegistry& builders, bool rebuild,
                                              ExportStats* outStats = nullptr,
                                              const ExportProgress& onProgress = {},
                                              const HashMap<Guid, Array<byte>>* sceneStreams = nullptr)
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
        const Status s = ExportContent(project, outDir, stats, onProgress, sceneStreams);
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
        PruningReport pruning;       // closure-pruning report (roots + reasons, kept/dropped counts).
                                     // pruned=false for a pack-everything (non-pruned) export.
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
                                          const HashMap<Guid, Array<byte>>* sceneStreams = nullptr,
                                          const SceneReferenceScanner* scanner = nullptr,
                                          const Array<Guid>* precomputedReachableRoots = nullptr)
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

        // Closure pruning is opt-in per preset. It needs the scene->asset edges discovered - EITHER
        // by a live scene-reference scanner (the CLI, which loads scenes on its own single thread),
        // OR by a precomputed reachable-root set (the editor, which pre-scans on the MAIN thread and
        // passes the guids into this background job - scene loading is main-thread-only). Without
        // either we must NOT silently drop content, so fall back to pack-everything with a warning.
        const bool haveScanner = (scanner != nullptr && *scanner);
        const bool havePrecomputed = (precomputedReachableRoots != nullptr);
        bool prune = preset.pruneToReachable;
        if (prune && !haveScanner && !havePrecomputed)
        {
            DRACONIC_LOG_WARNING(u8"Export",
                u8"preset '{}' requests pruning but no scene-reference scanner or precomputed root "
                u8"set was supplied - exporting everything", preset.name);
            prune = false;
        }

        Status contentStatus;
        if (prune)
        {
            // Seed roots -> expand scene-graph edges -> PlanFor closure -> cook + stage/pack only it.
            // The scene-edge expansion is either precomputed (editor main-thread pre-scan) or run
            // inline via the scanner (CLI). `seeds` is still recomputed here for the report (metadata
            // only, background-safe); it drives display, while planRoots drives cook/pack.
            const Array<ExportRoot> seeds = CollectExportRoots(project);
            const Array<Guid> planRoots = havePrecomputed
                ? *precomputedReachableRoots
                : ExpandReachableRoots(project, seeds, *scanner);
            Array<Guid> reachableList;
            contentStatus = CookReachable(project, builders, Span<const Guid>(planRoots.Data(), planRoots.Size()),
                                          cook, rebuild, result.content, reachableList, onProgress);
            if (contentStatus.IsOk())
            {
                HashMap<Guid, u8> reachable;
                for (const Guid& g : reachableList) { reachable.InsertOrAssign(g, u8(1)); }
                contentStatus = ExportContent(project, result.outputDir.AsView(), result.content, onProgress,
                                              sceneStreams, &reachable, &seeds, &result.pruning);
            }
        }
        else
        {
            contentStatus = cook
                ? ExportProject(project, result.outputDir.AsView(), builders, rebuild, &result.content, onProgress)
                : ExportContent(project, result.outputDir.AsView(), result.content, onProgress,
                                sceneStreams);
        }
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

        // Symbols (PDB/DWARF) are stripped from the dist by default; stage them only when the preset
        // opts in (export-templates.md symbols policy). sidecars[] always stage; symbols[] gate here.
        if (preset.stageSymbols)
        {
            for (const String& symbol : tmpl->symbols)
            {
                if (detail::CopyFilePreserving(tmpl->directory.AsView(), symbol.AsView(),
                                               result.outputDir.AsView(), symbol.AsView()))
                {
                    ++result.filesStaged;
                }
                else
                {
                    DRACONIC_LOG_WARNING(u8"Export", u8"symbol file '{}' not found in template '{}'", symbol, tmpl->id);
                }
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
                                          const HashMap<Guid, Array<byte>>* sceneStreams = nullptr,
                                          const SceneReferenceScanner* scanner = nullptr,
                                          const Array<Guid>* precomputedReachableRoots = nullptr)
    {
        // The reachable-root closure is project-level (not per-preset), so one precomputed set
        // seeds every pruning preset in the run.
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
                          sceneStreams, scanner, precomputedReachableRoots).IsOk())
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
