// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Core - :export partition.
//
// The export pipeline as a LIBRARY (the Tools.Export CLI and the editor's Export menu are
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

module editor.core;

import foundation.core;
import foundation.vfs;
import foundation.vfs.pak;
import foundation.content;
import engine.project;
import foundation.scene.resource;
import pipeline.core;
import pipeline.cook;
import foundation.shaders;
import :project;
import :export_preset;
import :export_roots;
import :export_template;

using namespace foundation::core;
using namespace foundation;
using namespace pipeline;
namespace shaders = foundation::shaders;

namespace editor
{
    namespace
    {
        // The cooked-blob formats a target platform's runtime needs: web = WGSL, Windows = SPIR-V
        // (Vulkan) + DXIL (DX12), other desktop = SPIR-V (Vulkan). Kept generous rather than guessing
        // the backend a desktop dist will pick at runtime.
        Array<shaders::CookedShaderFormat> FormatsForPlatform(StringView platform)
        {
            Array<shaders::CookedShaderFormat> formats;
            if (platform.StartsWith(u8"Web") || platform.StartsWith(u8"Wasm"))
            {
                formats.PushBack(shaders::CookedShaderFormat::Wgsl);
            }
            else if (platform.StartsWith(u8"Win"))
            {
                formats.PushBack(shaders::CookedShaderFormat::SpirV);
                formats.PushBack(shaders::CookedShaderFormat::Dxil);
            }
            else
            {
                formats.PushBack(shaders::CookedShaderFormat::SpirV);
            }
            return formats;
        }

        // The DXC runtime libs (dxcompiler / dxil), which a cooked dist no longer needs - the shader
        // pack retires the runtime compiler. Matched by substring on the sidecar's file name.
        bool IsDxcRuntimeLib(StringView name)
        {
            const auto contains = [&](StringView needle)
            {
                if (needle.Size() > name.Size())
                {
                    return false;
                }
                for (usize i = 0; i + needle.Size() <= name.Size(); ++i)
                {
                    if (name.SubStr(i, needle.Size()) == needle)
                    {
                        return true;
                    }
                }
                return false;
            };
            return contains(u8"dxcompiler") || contains(u8"dxil");
        }

        // Resolve the engine shader source root for the cook (baked source path, else a "Shaders"
        // dir beside a relocated editor).
        StringView EngineShaderDir()
        {
#ifdef BUILTIN_ENGINE_SHADER_DIR
            constexpr StringView baked = u8"" BUILTIN_ENGINE_SHADER_DIR;
#else
            constexpr StringView baked = u8"Shaders";
#endif
            if (DirectoryExists(baked))
            {
                return baked;
            }
            return u8"Shaders";
        }

        // Cook the built-in shaders for `platform` and write <outputDir>/shaders.dpak. The dist
        // renders from this pack with no runtime compiler (see the ShaderSystem cooked path). Returns
        // the variant count via `outVariants`; Status carries any cook/compile failure.
        Status StageShaderPack(StringView outputDir, StringView platform, u32& outVariants)
        {
            outVariants = 0;
            shaders::Compiler* compiler = nullptr;
            if (!shaders::createCompiler(shaders::CompilerDesc{}, compiler).IsOk() ||
                compiler == nullptr)
            {
                LOG_ERROR(u8"Export",
                                   u8"cannot cook shaders: the DXC compiler is unavailable");
                return Status{ErrorCode::Internal};
            }

            const Array<shaders::CookedShaderFormat> formats = FormatsForPlatform(platform);
            shaders::ShaderCookOptions opts;
            opts.shaderDir = EngineShaderDir();
            opts.scratchDir = outputDir; // WGSL intermediates (deleted); unused for SPIR-V/DXIL
            opts.formats = Span<const shaders::CookedShaderFormat>(formats.Data(), formats.Size());

            shaders::CookedShaderPack pack;
            const shaders::ShaderCookReport report =
                shaders::CookEngineShaders(*compiler, opts, pack);
            compiler->Destroy();

            for (usize i = 0; i < report.errors.Size(); ++i)
            {
                LOG_ERROR(u8"Export", u8"shader cook: {}", report.errors[i]);
            }
            if (!report.success)
            {
                return Status{ErrorCode::Internal};
            }

            const String packPath = PathJoin(outputDir, u8"shaders.dpak");
            FileStream out(packPath.AsView(), FileMode::Write);
            if (!out.IsValid() || !pack.Write(out).IsOk())
            {
                LOG_ERROR(u8"Export", u8"could not write shaders.dpak to '{}'", outputDir);
                return Status{ErrorCode::Internal};
            }
            outVariants = static_cast<u32>(pack.Count());
            return Status{};
        }
    }

    StringView ExportRootReasonName(ExportRootReason r)
    {
        switch (r)
        {
        case ExportRootReason::DefaultScene:
            return u8"default-scene";
        case ExportRootReason::StartupScript:
            return u8"startup-script";
        case ExportRootReason::Flag:
            return u8"always-export";
        case ExportRootReason::Group:
            return u8"always-export-group";
        case ExportRootReason::ManifestDefault:
            return u8"manifest-default";
        }
        return u8"?";
    }

    Array<ExportRoot> CollectExportRoots(EditorProject& project)
    {
        Array<ExportRoot> roots;
        HashMap<Guid, u8> seen;
        const auto add = [&](const Guid& id, ExportRootReason reason)
        {
            if (id.IsNil() || seen.Find(id) != nullptr)
            {
                return;
            }
            seen.InsertOrAssign(id, u8(1));
            ExportRoot root;
            root.id = id;
            root.reason = reason;
            if (foundation::content::Instance* inst = project.SourceDb().GetInstance(id))
            {
                root.name = inst->Path();
            }
            roots.PushBack(Move(root));
        };

        const engine::project::ProjectSettings& settings = project.Settings();

        add(settings.defaultSceneId, ExportRootReason::DefaultScene);

        // The startup game script is a cooked ScriptClass asset (guid-authoritative) - seed it as a
        // reachability root so it (and the assets IT loads, via the normal AssetRef contract) ship.
        add(settings.startupScriptId, ExportRootReason::StartupScript);

        // Every manifest default the PLAYER binds at startup must ship, or the binding silently
        // fails in a pruned dist (the theme/input-map/bus-layout gap existed before the font).
        add(settings.defaultInputMapId, ExportRootReason::ManifestDefault);
        add(settings.defaultBusLayoutId, ExportRootReason::ManifestDefault);
        add(settings.defaultUiThemeId, ExportRootReason::ManifestDefault);
        add(settings.defaultUiFontId, ExportRootReason::ManifestDefault);

        // Phase 2 "Always Export": explicit instance flags, then group subtrees (dynamic membership -
        // whatever is under the flagged folder now). A group that also contains the default scene /
        // a directly-flagged instance is deduped above, keeping the earlier reason.
        const ExportRootsSet& always = project.ExportRoots();
        for (const Guid& id : always.instances)
        {
            add(id, ExportRootReason::Flag);
        }
        for (const String& groupPath : always.groups)
        {
            Array<Guid> members;
            CollectGroupInstances(project.SourceDb(), groupPath.AsView(), members);
            for (const Guid& id : members)
            {
                add(id, ExportRootReason::Group);
            }
        }
        return roots;
    }

    Array<Guid> ExpandReachableRoots(EditorProject& project, const Array<ExportRoot>& seeds,
                                     const SceneReferenceScanner& scanner)
    {
        Array<Guid> out;
        HashMap<Guid, u8> seen;
        Array<Guid> queue;
        const auto push = [&](const Guid& id)
        {
            if (id.IsNil() || seen.Find(id) != nullptr)
            {
                return;
            }
            seen.InsertOrAssign(id, u8(1));
            out.PushBack(id);
            queue.PushBack(id);
        };
        for (const ExportRoot& r : seeds)
        {
            push(r.id);
        }

        usize head = 0;
        while (head < queue.Size())
        {
            const Guid id = queue[head++];
            foundation::content::Instance* inst = project.SourceDb().GetInstance(id);
            if (inst == nullptr)
            {
                continue;
            }
            const bool isSceneLike =
                inst->TypeName() == u8"SceneDocument" || inst->TypeName() == u8"PrefabDocument";
            if (!isSceneLike || !scanner)
            {
                continue;
            }

            SceneReferences refs;
            scanner(*inst, project.SourceDb(), refs);
            for (const Guid& g : refs.resources)
            {
                push(g);
            }
            for (const Guid& g : refs.prefabs)
            {
                push(g);
            }
        }
        return out;
    }

    Status CookReachable(EditorProject& project, BuilderRegistry& builders,
                         Span<const Guid> planRoots, bool cook, bool rebuild, ExportStats& stats,
                         Array<Guid>& outReachable, const ExportProgress& onProgress)
    {
        foundation::vfs::NativeFileSystem sourcesMount(project.SourcesRoot().AsView(), editor::EditorRootAllocator());
        foundation::vfs::NativeFileSystem cacheMount(project.CacheRoot().AsView(), editor::EditorRootAllocator());
        JobSystem jobs(editor::EditorRootAllocator()); // export tool root
        CookDriver driver(editor::EditorRootAllocator(), project.SourceDb(), project.CookedDb(),
                          builders, &sourcesMount, &cacheMount, &jobs);
        CookPlan plan = driver.PlanFor(planRoots, rebuild);
        outReachable = plan.reachable;

        if (cook)
        {
            CookProgress cookProgress;
            cookProgress.onItem = [&onProgress](usize done, usize total, StringView path, bool)
            {
                if (!onProgress)
                {
                    return;
                }
                const f32 frac =
                    (total > 0) ? 0.05f + (static_cast<f32>(done) / static_cast<f32>(total)) * 0.55f
                                : 0.6f;
                String step(u8"Cooking ");
                step += path;
                onProgress(step.AsView(), frac);
            };
            const CookStats cookStats = driver.Execute(plan, &cookProgress);
            stats.cooked = cookStats.cooked;
            stats.cookFailed = cookStats.failed;
            if (cookStats.failed > 0)
            {
                LOG_ERROR(u8"Export", u8"aborting - the cook has {} failure(s)",
                                   cookStats.failed);
                return Status{ErrorCode::Internal};
            }
        }
        return Status{};
    }

    String FormatPruningReport(const PruningReport& report)
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

    // Is this a web/wasm target (the only one that splits into BC + ASTC variants today)?
    static bool IsWebPlatform(StringView platform)
    {
        return platform.StartsWith(u8"Web") || platform.StartsWith(u8"Wasm");
    }

    Array<ContentVariant> VariantsForPlatform(EditorProject& project, StringView platform)
    {
        Array<ContentVariant> out;
        if (IsWebPlatform(platform))
        {
            // Two COMPLETE paks - one per compressed-family the browser might support (Decision 4).
            // Cooked dirs are SIBLINGS of the host Cooked/ (Fable Q2), matching Tools.Cook --target.
            const StringView keys[] = {StringView(u8"bc"), StringView(u8"astc")};
            for (const StringView key : keys)
            {
                ContentVariant v;
                v.key = String(key);
                v.pakName = String(u8"Content-");
                v.pakName.Append(key);
                v.pakName.Append(u8".pak");
                v.cookedDir = String(project.Directory());
                v.cookedDir.Append(u8"/Cooked-web-");
                v.cookedDir.Append(key); // Cooked-web-bc / Cooked-web-astc
                out.PushBack(Move(v));
            }
        }
        else
        {
            // Desktop: the single host pak from Cooked/.
            ContentVariant v;
            v.pakName = String(engine::project::kDistContentPak);
            v.cookedDir = PathJoin(project.Directory(), engine::project::kProjectCookedDir);
            out.PushBack(Move(v));
        }
        return out;
    }

    Status CookVariantTargets(EditorProject& project, BuilderRegistry& builders,
                              Span<const ContentVariant> variants, bool rebuild,
                              const ExportProgress& onProgress)
    {
        // The host cook already ran (ExportProject/CookReachable) and persisted its records to
        // .cache/cook.db - load them once to gate copy-forward for every target.
        foundation::vfs::NativeFileSystem cacheMount(project.CacheRoot().AsView(), editor::EditorRootAllocator());
        CookDb hostRecords{editor::EditorRootAllocator()};
        hostRecords.Load(cacheMount);

        foundation::vfs::NativeFileSystem sourcesMount(project.SourcesRoot().AsView(), editor::EditorRootAllocator());
        JobSystem jobs(editor::EditorRootAllocator()); // export tool root
        usize failed = 0;
        for (const ContentVariant& v : variants)
        {
            if (v.key.IsEmpty())
            {
                continue; // desktop: the host DB already IS this variant
            }
            if (onProgress)
            {
                String step(u8"Cooking variant ");
                step += v.key.AsView();
                onProgress(step.AsView(), 0.6f);
            }
            // Target id "web-<key>" resolves to the capability profile (web-astc -> ASTC, else BC).
            String targetId(u8"web-");
            targetId += v.key.AsView();

            // Materialize Cooked-web-<key>/ + .cache/web-<key>/ ; carry invariants forward from host.
            const String cacheDir = PathJoin(
                PathJoin(project.Directory(), engine::project::kProjectCacheDir), targetId.AsView());
            (void)CreateDirectory(v.cookedDir.AsView());
            (void)CreateDirectory(PathJoin(project.Directory(),
                                           engine::project::kProjectCacheDir).AsView());
            (void)CreateDirectory(cacheDir.AsView());

            foundation::vfs::NativeFileSystem targetCookedMount(v.cookedDir.AsView(), editor::EditorRootAllocator());
            foundation::vfs::NativeFileSystem targetCacheMount(cacheDir.AsView(), editor::EditorRootAllocator());
            foundation::content::ContentDatabase targetDb(editor::EditorRootAllocator(), targetCookedMount, BinarySerializerFactory(),
                                                        engine::project::kCookedAssetExtension);

            const CookStats s = CookForTarget(editor::EditorRootAllocator(), project.SourceDb(),
                                              targetDb, project.CookedDb(),
                                              hostRecords, builders, &sourcesMount, &targetCacheMount,
                                              CookTargetFor(targetId.AsView()), &jobs, rebuild);
            failed += s.failed;
            LOG_INFO(u8"Export", u8"variant '{}' cooked {}, copied-forward {}, failed {}", v.key,
                     s.cooked, s.copiedForward, s.failed);
        }
        return (failed == 0) ? Status{} : Status{ErrorCode::Internal};
    }

    Status ExportContent(EditorProject& project, StringView outDir, ExportStats& stats,
                         const ExportProgress& onProgress,
                         const HashMap<Guid, Array<byte>>* sceneStreams,
                         const HashMap<Guid, u8>* reachable, const Array<ExportRoot>* roots,
                         PruningReport* outReport, Span<const ContentVariant> variants)
    {

        // --- stage scenes ---
        if (onProgress)
        {
            onProgress(u8"Staging scenes...", 0.65f);
        }
        if (!CreateDirectory(outDir))
        {
            return Status{ErrorCode::NotSupported};
        }
        const String stagingDir = PathJoin(outDir, u8".stage-scenes");
        (void)CreateDirectory(stagingDir.AsView());
        foundation::vfs::NativeFileSystem stagingMount(stagingDir.AsView(), editor::EditorRootAllocator());
        Array<String> droppedScenes; // for the pruning report
        {
            foundation::content::ContentDatabase staging(editor::EditorRootAllocator(), stagingMount, BinarySerializerFactory(),
                                                       engine::project::kCookedAssetExtension);
            Array<foundation::content::Instance*> scenes;
            detail::CollectScenes(*project.SourceDb().RootGroup(), scenes);
            usize staged = 0;
            for (foundation::content::Instance* scene : scenes)
            {
                if (reachable != nullptr && reachable->Find(scene->Id()) == nullptr)
                {
                    droppedScenes.PushBack(scene->Path()); // pruned: not reachable from any root
                    continue;
                }
                if (!detail::StageScene(*scene, staging, sceneStreams))
                {
                    LOG_ERROR(u8"Export", u8"failed to stage scene '{}'", scene->Path());
                    return Status{ErrorCode::Internal};
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
            reachablePaths = MakeUnique<HashMap<String, u8>>(editor::EditorRootAllocator());
            Array<foundation::content::Instance*> cooked;
            detail::CollectAllInstances(*project.CookedDb().RootGroup(), cooked);
            for (foundation::content::Instance* product : cooked)
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
            Array<foundation::content::Instance*> sources;
            detail::CollectAllInstances(*project.SourceDb().RootGroup(), sources);
            for (foundation::content::Instance* src : sources)
            {
                const bool isSceneLike =
                    src->TypeName() == u8"SceneDocument" || src->TypeName() == u8"PrefabDocument";
                if (!isSceneLike && reachable->Find(src->Id()) == nullptr)
                {
                    droppedProducts.PushBack(src->Path());
                }
            }
        }

        if (onProgress)
        {
            onProgress(u8"Packing content...", 0.78f);
        }
        // The variants to pack: the caller's list, or - by default - the single host Content.pak.
        // Each variant is a COMPLETE pak: its own cooked DB + the SAME
        // staged scenes + the SAME reachable set (the closure guid-set is variant-invariant).
        Array<ContentVariant> effectiveVariants;
        if (variants.IsEmpty())
        {
            ContentVariant desktop;
            desktop.pakName = String(engine::project::kDistContentPak);
            desktop.cookedDir = PathJoin(project.Directory(), engine::project::kProjectCookedDir);
            effectiveVariants.PushBack(Move(desktop));
        }
        else
        {
            for (const ContentVariant& v : variants)
            {
                ContentVariant copy;
                copy.key = String(v.key.AsView());
                copy.pakName = String(v.pakName.AsView());
                copy.cookedDir = String(v.cookedDir.AsView());
                effectiveVariants.PushBack(Move(copy));
            }
        }
        // The startup game script needs no special staging - it is a cooked ScriptClass asset in the
        // reachability closure, so it already rides in the content DB pak like every other asset. The
        // dist manifest carries its guid (below); the player binds it from the content DB.
        for (const ContentVariant& v : effectiveVariants)
        {
            foundation::vfs::PakBuilder pak;
            foundation::vfs::NativeFileSystem cookedMount(v.cookedDir.AsView(), editor::EditorRootAllocator());
            if (!detail::PackTree(cookedMount, *cookedMount.AsEnumerable(), u8"", pak,
                                  stats.filesPacked, reachablePaths.Get()) ||
                !detail::PackTree(stagingMount, *stagingMount.AsEnumerable(), u8"", pak,
                                  stats.filesPacked))
            {
                LOG_ERROR(u8"Export", u8"packing failed for variant '{}'", v.pakName);
                return Status{ErrorCode::Internal};
            }
            const String pakPath = PathJoin(outDir, v.pakName.AsView());
            if (!pak.Write(pakPath.AsView()).IsOk())
            {
                LOG_ERROR(u8"Export", u8"failed to write '{}'", v.pakName);
                return Status{ErrorCode::Internal};
            }
        }

        // --- 4. dist manifest ---
        if (onProgress)
        {
            onProgress(u8"Writing manifest...", 0.9f);
        }
        {
            foundation::vfs::NativeFileSystem outMount(outDir, editor::EditorRootAllocator());
            engine::project::ProjectSettings dist;
            dist.name = String(project.Settings().name.AsView());
            dist.defaultSceneId = project.Settings().defaultSceneId;
            dist.defaultScene = String(project.Settings().defaultScene.AsView());
            dist.startupScriptId = project.Settings().startupScriptId;
            dist.startupScript =
                String(project.Settings().startupScript.AsView()); // display mirror
            // The manifest defaults the player binds at startup - without them the
            // theme/input-map/bus-layout bindings could never fire in an exported build.
            dist.defaultInputMapId = project.Settings().defaultInputMapId;
            dist.defaultBusLayoutId = project.Settings().defaultBusLayoutId;
            dist.defaultUiThemeId = project.Settings().defaultUiThemeId;
            dist.defaultUiFontId = project.Settings().defaultUiFontId;
            if (!engine::project::SaveProjectSettings(*outMount.AsWritable(), dist,
                                                        engine::project::kDistManifestFile)
                     .IsOk())
            {
                LOG_ERROR(u8"Export", u8"failed to write the dist manifest");
                return Status{ErrorCode::Internal};
            }
        }

        // --- 5. pruning report (loud + auditable: pruning can silently break an exported game) ---
        if (reachable != nullptr)
        {
            PruningReport report;
            report.pruned = true;
            if (roots != nullptr)
            {
                report.roots = *roots;
            }
            report.keptCount = stats.scenesStaged + keptProducts;
            for (const String& d : droppedScenes)
            {
                report.dropped.PushBack(String(d.AsView()));
            }
            for (const String& d : droppedProducts)
            {
                report.dropped.PushBack(String(d.AsView()));
            }

            const String text = FormatPruningReport(report);
            LOG_INFO(u8"Export", u8"pruned dist: {} kept, {} dropped ({} root(s))",
                              report.keptCount, report.dropped.Size(), report.roots.Size());
            {
                foundation::vfs::NativeFileSystem outMount(outDir, editor::EditorRootAllocator());
                (void)outMount.AsWritable()->Save(
                    u8"export-report.txt",
                    Span<const byte>(reinterpret_cast<const byte*>(text.CStr()), text.Size()));
            }
            if (outReport != nullptr)
            {
                *outReport = Move(report);
            }
        }

        detail::RemoveTreeRecursive(stagingDir.AsView());
        return Status{};
    }

    Status ExportProject(EditorProject& project, StringView outDir, BuilderRegistry& builders,
                         bool rebuild, ExportStats* outStats, const ExportProgress& onProgress,
                         const HashMap<Guid, Array<byte>>* sceneStreams,
                         Span<const ContentVariant> variants)
    {
        ExportStats stats;

        // --- cook ---
        if (onProgress)
        {
            onProgress(u8"Cooking content...", 0.05f);
        }
        foundation::vfs::NativeFileSystem sourcesMount(project.SourcesRoot().AsView(), editor::EditorRootAllocator());
        foundation::vfs::NativeFileSystem cacheMount(project.CacheRoot().AsView(), editor::EditorRootAllocator());
        JobSystem jobs(editor::EditorRootAllocator()); // export tool root
        CookDriver driver(editor::EditorRootAllocator(), project.SourceDb(), project.CookedDb(),
                          builders, &sourcesMount, &cacheMount, &jobs);
        CookPlan plan = driver.Plan(rebuild);
        CookProgress cookProgress;
        cookProgress.onItem = [&onProgress](usize done, usize total, StringView path, bool)
        {
            if (!onProgress)
            {
                return;
            }
            const f32 frac =
                (total > 0) ? 0.05f + (static_cast<f32>(done) / static_cast<f32>(total)) * 0.55f
                            : 0.6f; // cook occupies 0.05..0.60 of the export
            String step(u8"Cooking ");
            step += path;
            onProgress(step.AsView(), frac);
        };
        const CookStats cookStats = driver.Execute(plan, &cookProgress);
        stats.cooked = cookStats.cooked;
        stats.cookFailed = cookStats.failed;
        if (cookStats.failed > 0)
        {
            LOG_ERROR(u8"Export", u8"aborting - the cook has {} failure(s)",
                               cookStats.failed);
            if (outStats != nullptr)
            {
                *outStats = stats;
            }
            return Status{ErrorCode::Internal};
        }

        // --- variant target cooks (web: BC + ASTC) - after the host cook, before the pack ---
        if (!variants.IsEmpty())
        {
            const Status vs = CookVariantTargets(project, builders, variants, rebuild, onProgress);
            if (!vs.IsOk())
            {
                LOG_ERROR(u8"Export", u8"aborting - a variant cook failed");
                if (outStats != nullptr)
                {
                    *outStats = stats;
                }
                return Status{ErrorCode::Internal};
            }
        }

        // --- content ---
        const Status s =
            ExportContent(project, outDir, stats, onProgress, sceneStreams, nullptr, nullptr,
                          nullptr, variants);
        if (outStats != nullptr)
        {
            *outStats = stats;
        }
        return s;
    }

    namespace
    {
        // Build Engine.GamePlayer for a native project (game-native-code.md N3): re-invoke
        // cmake over the engine checkout with the game's native dir wired in
        // (ENGINE_GAME_NATIVE_DIR/_TARGET), a persistent per-project build dir under the
        // project's .cache (incremental relinks), and a per-project Bin suffix so ship
        // outputs never collide with dev builds. Returns the built player's path.
        [[nodiscard]] Status BuildShipPlayer(EditorProject& project, StringView config,
                                             const ExportProgress& onProgress, String& outPlayer)
        {
            const StringView engineRoot =
                StringView(reinterpret_cast<const char8_t*>(BUILDSYSTEM_ENGINE_ROOT));
            const StringView cmakePath =
                StringView(reinterpret_cast<const char8_t*>(BUILDSYSTEM_CMAKE_PATH));
            if (!DirectoryExists(PathJoin(engineRoot, u8"Code").AsView()))
            {
                LOG_ERROR(u8"Export",
                          u8"native ship link needs the engine source checkout at '{}' - not "
                          u8"found (relocated editor?)",
                          engineRoot);
                return Status{ErrorCode::NotFound};
            }
            const String nativeDir = PathJoin(project.Directory(), u8"Native");
            if (!DirectoryExists(nativeDir.AsView()))
            {
                LOG_ERROR(u8"Export",
                          u8"project declares a native module but '{}' does not exist "
                          u8"(the native source convention is <project>/Native)",
                          nativeDir);
                return Status{ErrorCode::NotFound};
            }
            const String target =
                detail::NativeTargetFromModulePath(project.Settings().nativeModule.AsView());
            if (target.IsEmpty())
            {
                LOG_ERROR(u8"Export", u8"cannot derive the native target from nativeModule '{}'",
                          project.Settings().nativeModule);
                return Status{ErrorCode::InvalidArgument};
            }
            const String suffix = detail::ShipOutputSuffix(project.Settings().name.AsView());
            const String buildType = config.IsEmpty() ? String(u8"Release") : String(config);
            const String shipDir = PathJoin(
                project.Directory(),
                Format(u8"{}/ship-{}", engine::project::kProjectCacheDir, buildType).AsView());

            if (onProgress)
            {
                onProgress(u8"Configuring native ship build...", 0.86f);
            }
            // Pin the ship toolchain to the one that built THIS editor (the system default
            // may be a different compiler; the checkout's lane conventions stay authoritative).
            const String defCompiler =
                Format(u8"-DCMAKE_CXX_COMPILER={}",
                       StringView(reinterpret_cast<const char8_t*>(BUILDSYSTEM_CXX_COMPILER)));
            const String defBuildType = Format(u8"-DCMAKE_BUILD_TYPE={}", buildType);
            const String defGameDir = Format(u8"-DENGINE_GAME_NATIVE_DIR={}", nativeDir);
            const String defGameTarget = Format(u8"-DENGINE_GAME_NATIVE_TARGET={}", target);
            const String defSuffix = Format(u8"-DBUILDSYSTEM_OUTPUT_SUFFIX={}", suffix);
            const StringView cfgArgs[] = {u8"-S",       engineRoot,
                                          u8"-B",       shipDir.AsView(),
                                          u8"-G",       u8"Ninja",
                                          defCompiler.AsView(),  defBuildType.AsView(),
                                          defGameDir.AsView(),   defGameTarget.AsView(),
                                          defSuffix.AsView()};
            const ProcessResult configured =
                RunProcess(cmakePath, Span<const StringView>(cfgArgs, 11));
            if (!configured.Ok())
            {
                LOG_ERROR(u8"Export", u8"native ship configure failed ({}):\n{}",
                          configured.exitCode, configured.output);
                return Status{ErrorCode::Internal};
            }

            if (onProgress)
            {
                onProgress(u8"Building native ship player (this can take a while)...", 0.88f);
            }
            const StringView buildArgs[] = {u8"--build", shipDir.AsView(), u8"--target",
                                            u8"Engine.GamePlayer", u8"-j", u8"4"};
            const ProcessResult built =
                RunProcess(cmakePath, Span<const StringView>(buildArgs, 6));
            if (!built.Ok())
            {
                LOG_ERROR(u8"Export", u8"native ship build failed ({}) - tail:\n{}",
                          built.exitCode, built.output);
                return Status{ErrorCode::Internal};
            }

            // The player lands under the CHECKOUT's suffixed Bin; the platform-compiler tag is
            // the build's own concern, so find it rather than recompose it.
            const String binRoot = Format(u8"{}/Bin/{}", engineRoot, buildType);
            vfs::NativeFileSystem bin(binRoot.AsView(), editor::EditorRootAllocator());
            if (vfs::IEnumerableFileSystem* enumerable = bin.AsEnumerable())
            {
                Array<vfs::DirEntry> entries;
                if (enumerable->Enumerate(u8"", entries).IsOk())
                {
                    for (const vfs::DirEntry& entry : entries)
                    {
                        if (entry.isDirectory && entry.name.AsView().EndsWith(suffix.AsView()))
                        {
                            const String candidate = Format(u8"{}/{}/Engine.GamePlayer", binRoot,
                                                            entry.name);
                            if (FileExists(candidate.AsView()))
                            {
                                outPlayer = candidate;
                                return Status{};
                            }
                        }
                    }
                }
            }
            LOG_ERROR(u8"Export", u8"native ship build succeeded but Engine.GamePlayer was not "
                                  u8"found under '{}/*{}'",
                      binRoot, suffix);
            return Status{ErrorCode::NotFound};
        }
    }

    Status BuildDevNativeModule(EditorProject& project)
    {
        const StringView engineRoot =
            StringView(reinterpret_cast<const char8_t*>(BUILDSYSTEM_ENGINE_ROOT));
        const StringView cmakePath =
            StringView(reinterpret_cast<const char8_t*>(BUILDSYSTEM_CMAKE_PATH));
        if (project.Settings().nativeModule.IsEmpty())
        {
            return Status{ErrorCode::NotFound};
        }
        if (!DirectoryExists(PathJoin(engineRoot, u8"Code").AsView()))
        {
            LOG_ERROR(u8"Export", u8"native dev build needs the engine checkout at '{}'",
                      engineRoot);
            return Status{ErrorCode::NotFound};
        }
        const String nativeDir = PathJoin(project.Directory(), u8"Native");
        const String target =
            detail::NativeTargetFromModulePath(project.Settings().nativeModule.AsView());
        if (target.IsEmpty() || !DirectoryExists(nativeDir.AsView()))
        {
            return Status{ErrorCode::InvalidArgument};
        }
        const String suffix =
            Format(u8"-Dev-{}", detail::ShipOutputSuffix(project.Settings().name.AsView())
                                    .AsView()
                                    .Data() + 6); // reuse the sanitizer, swap the prefix
        const String devDir = PathJoin(
            project.Directory(),
            Format(u8"{}/native-dev", engine::project::kProjectCacheDir).AsView());

        const String defCompiler =
            Format(u8"-DCMAKE_CXX_COMPILER={}",
                   StringView(reinterpret_cast<const char8_t*>(BUILDSYSTEM_CXX_COMPILER)));
        const String defGameDir = Format(u8"-DENGINE_GAME_NATIVE_DIR={}", nativeDir);
        const String defGameTarget = Format(u8"-DENGINE_GAME_NATIVE_TARGET={}", target);
        const String defSuffix = Format(u8"-DBUILDSYSTEM_OUTPUT_SUFFIX={}", suffix);
        const StringView cfgArgs[] = {u8"-S",
                                      engineRoot,
                                      u8"-B",
                                      devDir.AsView(),
                                      u8"-G",
                                      u8"Ninja",
                                      defCompiler.AsView(),
                                      u8"-DCMAKE_BUILD_TYPE=Debug",
                                      u8"-DENGINE_SHARED_LIBS=ON",
                                      defGameDir.AsView(),
                                      defGameTarget.AsView(),
                                      defSuffix.AsView()};
        const ProcessResult configured =
            RunProcess(cmakePath, Span<const StringView>(cfgArgs, 12));
        if (!configured.Ok())
        {
            LOG_ERROR(u8"Export", u8"native dev configure failed ({}):\n{}",
                      configured.exitCode, configured.output);
            return Status{ErrorCode::Internal};
        }
        const StringView buildArgs[] = {u8"--build", devDir.AsView(), u8"--target",
                                        target.AsView(), u8"-j", u8"4"};
        const ProcessResult built = RunProcess(cmakePath, Span<const StringView>(buildArgs, 6));
        if (!built.Ok())
        {
            LOG_ERROR(u8"Export", u8"native dev build failed ({}) - tail:\n{}",
                      built.exitCode, built.output);
            return Status{ErrorCode::Internal};
        }
        // Success = the .so is at the manifest path (the Native/CMakeLists output-dir rule).
        const String modulePath =
            PathJoin(project.Directory(), project.Settings().nativeModule.AsView());
        return FileExists(modulePath.AsView()) ? Status{} : Status{ErrorCode::NotFound};
    }

    Status ExportOne(EditorProject& project, const ExportPreset& preset,
                     const TemplateRegistry& templates, BuilderRegistry& builders,
                     StringView outRoot, bool rebuild, ExportResult* outResult,
                     const ExportProgress& onProgress, bool cook,
                     const HashMap<Guid, Array<byte>>* sceneStreams,
                     const SceneReferenceScanner* scanner,
                     const Array<Guid>* precomputedReachableRoots)
    {
        const ExportTemplate* tmpl = templates.Resolve(preset);
        if (tmpl == nullptr)
        {
            LOG_ERROR(u8"Export",
                               u8"no export template for preset '{}' (platform '{}') - import one",
                               preset.name, preset.platform);
            return Status{ErrorCode::NotFound};
        }

        ExportResult result;

        // Soft engine-version match (mirrors EditorProject::Open's project-manifest check): a template
        // built against a different engine version may be binary-incompatible with the cooked content,
        // but we don't know that it is - so warn and keep going rather than block. Empty version = an
        // older/hand-written template with no stamp; skip.
        if (!tmpl->engineVersion.IsEmpty() &&
            tmpl->engineVersion != engine::project::kEngineVersionString)
        {
            LOG_WARNING(u8"Export",
                                 u8"template '{}' was built against engine {} but this build is {} "
                                 u8"- exporting anyway",
                                 tmpl->id, tmpl->engineVersion,
                                 engine::project::kEngineVersionString);
            result.engineVersionWarning = String(u8"Template '");
            result.engineVersionWarning += tmpl->id;
            result.engineVersionWarning += u8"' targets engine ";
            result.engineVersionWarning += tmpl->engineVersion;
            result.engineVersionWarning += u8" (this build is ";
            result.engineVersionWarning += engine::project::kEngineVersionString;
            result.engineVersionWarning += u8").";
        }

        const String subdir = preset.outputSubdir.IsEmpty()
                                  ? detail::SanitizeName(preset.name.AsView())
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
            LOG_WARNING(
                u8"Export",
                u8"preset '{}' requests pruning but no scene-reference scanner or precomputed root "
                u8"set was supplied - exporting everything",
                preset.name);
            prune = false;
        }

        // The content variants this preset ships: desktop = the single host pak;
        // Web = BC + ASTC. Threaded into every content path so the pack produces one pak per variant.
        const Array<ContentVariant> variants =
            VariantsForPlatform(project, preset.platform.AsView());
        const Span<const ContentVariant> variantSpan(variants.Data(), variants.Size());

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
            contentStatus = CookReachable(project, builders,
                                          Span<const Guid>(planRoots.Data(), planRoots.Size()),
                                          cook, rebuild, result.content, reachableList, onProgress);
            // Variant target cooks (web) after the host closure cook; no-op for desktop.
            if (contentStatus.IsOk())
            {
                contentStatus =
                    CookVariantTargets(project, builders, variantSpan, rebuild, onProgress);
            }
            if (contentStatus.IsOk())
            {
                HashMap<Guid, u8> reachable;
                for (const Guid& g : reachableList)
                {
                    reachable.InsertOrAssign(g, u8(1));
                }
                contentStatus =
                    ExportContent(project, result.outputDir.AsView(), result.content, onProgress,
                                  sceneStreams, &reachable, &seeds, &result.pruning, variantSpan);
            }
        }
        else if (cook)
        {
            contentStatus = ExportProject(project, result.outputDir.AsView(), builders, rebuild,
                                          &result.content, onProgress, sceneStreams, variantSpan);
        }
        else
        {
            // The editor already cooked the HOST via CookService; still cook the variant targets.
            contentStatus = CookVariantTargets(project, builders, variantSpan, rebuild, onProgress);
            if (contentStatus.IsOk())
            {
                contentStatus =
                    ExportContent(project, result.outputDir.AsView(), result.content, onProgress,
                                  sceneStreams, nullptr, nullptr, nullptr, variantSpan);
            }
        }
        if (!contentStatus.IsOk())
        {
            if (outResult != nullptr)
            {
                *outResult = result;
            }
            return Status{ErrorCode::Internal};
        }

        if (onProgress)
        {
            onProgress(u8"Staging player...", 0.93f);
        }
        // Player: <template dir>/<playerBinary> -> <outDir>/<name>. The name is the preset's playerName
        // (else the template binary), with the target platform's executable extension ensured - Windows
        // needs .exe or the OS will not launch it.
        //
        // NATIVE projects (game-native-code.md N3): the player is not the template's - it is
        // Engine.GamePlayer, freshly built with the game's native module statically linked.
        // Web presets keep the template player (the wasm game build is N5); the manifest's
        // dlopen module is ignored by ship players (the static plugin takes precedence).
        const bool nativeShip = !project.Settings().nativeModule.IsEmpty() &&
                                !preset.platform.AsView().StartsWith(u8"Web") &&
                                !preset.platform.AsView().StartsWith(u8"Wasm");
        String shipPlayerPath;
        if (nativeShip)
        {
            const Status shipStatus = BuildShipPlayer(project, preset.config.AsView(), onProgress,
                                                      shipPlayerPath);
            if (!shipStatus.IsOk())
            {
                if (outResult != nullptr)
                {
                    *outResult = result;
                }
                return shipStatus;
            }
        }
        const String outName = detail::PlayerOutputName(
            preset.platform.AsView(), preset.playerName.AsView(),
            nativeShip ? StringView(u8"Engine.GamePlayer") : tmpl->playerBinary.AsView());
        String shipPlayerDir;
        if (nativeShip)
        {
            // Split the built player's absolute path into (dir, name) for the copy helper.
            const StringView full = shipPlayerPath.AsView();
            for (usize i = full.Size(); i > 0; --i)
            {
                if (full.Data()[i - 1] == '/')
                {
                    shipPlayerDir = String(StringView(full.Data(), i - 1));
                    break;
                }
            }
        }
        const bool playerStaged =
            nativeShip
                ? detail::CopyFilePreserving(shipPlayerDir.AsView(),
                                             StringView(u8"Engine.GamePlayer"),
                                             result.outputDir.AsView(), outName.AsView())
                : detail::CopyFilePreserving(tmpl->directory.AsView(), tmpl->playerBinary.AsView(),
                                             result.outputDir.AsView(), outName.AsView());
        if (!playerStaged)
        {
            LOG_ERROR(u8"Export", u8"failed to stage player '{}' from {}",
                               nativeShip ? StringView(u8"Engine.GamePlayer")
                                          : tmpl->playerBinary.AsView(),
                               nativeShip ? shipPlayerPath.AsView() : tmpl->id.AsView());
            if (outResult != nullptr)
            {
                *outResult = result;
            }
            return Status{ErrorCode::Internal};
        }
        ++result.filesStaged;

        // Cooked engine shaders: produce shaders.dpak beside the player so the dist renders with no
        // runtime compiler. A cook failure is fatal - a dist without shaders cannot render.
        if (onProgress)
        {
            onProgress(u8"Cooking shaders...", 0.95f);
        }
        {
            u32 shaderVariants = 0;
            const Status packStatus =
                StageShaderPack(result.outputDir.AsView(), preset.platform.AsView(), shaderVariants);
            if (!packStatus.IsOk())
            {
                if (outResult != nullptr)
                {
                    *outResult = result;
                }
                return Status{ErrorCode::Internal};
            }
            ++result.filesStaged;
            LOG_INFO(u8"Export", u8"staged shaders.dpak ({} variants)", shaderVariants);
        }

        if (onProgress && !tmpl->sidecars.IsEmpty())
        {
            onProgress(u8"Staging runtime libs...", 0.96f);
        }
        // Template sidecars (runtime libs) from the template dir. The cooked shader pack retires the
        // runtime DXC compiler, so its libs are dropped here - the dist ships no dxcompiler/dxil
        // (retires the DXC-runtime-sidecar fragility class for dists).
        for (const String& sidecar : tmpl->sidecars)
        {
            if (IsDxcRuntimeLib(sidecar.AsView()))
            {
                LOG_INFO(u8"Export", u8"omitting DXC sidecar '{}' (dist renders from the "
                                              u8"cooked shader pack)",
                                  sidecar);
                continue;
            }
            if (detail::CopyFilePreserving(tmpl->directory.AsView(), sidecar.AsView(),
                                           result.outputDir.AsView(), sidecar.AsView()))
            {
                ++result.filesStaged;
            }
            else
            {
                LOG_WARNING(u8"Export", u8"sidecar '{}' not found in template '{}'",
                                     sidecar, tmpl->id);
            }
        }

        // Symbols (PDB/DWARF) are stripped from the dist by default; stage them only when the preset
        // opts in. sidecars[] always stage; symbols[] gate here.
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
                    LOG_WARNING(u8"Export",
                                         u8"symbol file '{}' not found in template '{}'", symbol,
                                         tmpl->id);
                }
            }
        }

        // Preset additionalFiles (game extras, project-relative) -> <outDir>/<basename>.
        for (const String& extra : preset.additionalFiles)
        {
            const StringView base = detail::BaseName(extra.AsView());
            if (detail::CopyFilePreserving(project.Directory(), extra.AsView(),
                                           result.outputDir.AsView(), base))
            {
                ++result.filesStaged;
            }
            else
            {
                LOG_WARNING(u8"Export", u8"additional file '{}' not found", extra);
            }
        }

        if (onProgress)
        {
            onProgress(u8"Done", 1.0f);
        }
        if (outResult != nullptr)
        {
            *outResult = result;
        }
        return Status{};
    }

    Status ExportAll(EditorProject& project, Span<const ExportPreset> presets,
                     const TemplateRegistry& templates, BuilderRegistry& builders,
                     StringView outRoot, bool rebuild, const ExportProgress& onProgress, bool cook,
                     const HashMap<Guid, Array<byte>>* sceneStreams,
                     const SceneReferenceScanner* scanner,
                     const Array<Guid>* precomputedReachableRoots)
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
                if (!onProgress)
                {
                    return;
                }
                String s(preset.name.AsView());
                s += u8": ";
                s += step;
                onProgress(s.AsView(), (static_cast<f32>(i) + frac) / static_cast<f32>(n));
            };
            ExportResult result;
            if (ExportOne(project, preset, templates, builders, outRoot, rebuild, &result, scoped,
                          cook, sceneStreams, scanner, precomputedReachableRoots)
                    .IsOk())
            {
                ++ok;
                LOG_INFO(u8"Export", u8"exported '{}' -> {} ({} files staged)",
                                  preset.name, result.outputDir, result.filesStaged);
            }
            else
            {
                LOG_ERROR(u8"Export", u8"preset '{}' failed", preset.name);
            }
        }
        return (ok == n) ? Status{} : Status{ErrorCode::Internal};
    }
}
