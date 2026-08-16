// Tools.Cook - the command-line cooker (docs/design/asset-pipeline.md §6). Headless: opens the
// project, registers every builder, plans + executes the incremental cook.
//
// Usage: Tools.Cook <projectDirectory> [--rebuild] [--dry-run] [--target <id>]
//   --rebuild        force-cook every buildable asset (the "forgot the version bump" big hammer)
//   --dry-run        print the plan (dirty set + orphans) without cooking
//   --target <id>    cook a per-target DB under Cooked/<id>/ instead of the host DB (asset-variants
//                    P2). Cooks the host first, then carries platform-invariant products forward and
//                    recooks only variant products (textures: BC vs ASTC) for the target. "web-astc"
//                    is the ASTC mobile-web target; everything else is BC desktop. "host" = default.
// Exit code = number of failed cooks (0 = success).

#include <cstdio>
#include <cstring>
#include "Core/Log/Log.h"

import foundation.core;
import foundation.content;
import foundation.vfs;
import pipeline.core;
import pipeline.registration;
import editor.core;
import pipeline.cook;

using namespace foundation::core;
namespace vfs = foundation::vfs;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: Tools.Cook <projectDirectory> [--rebuild] [--dry-run]\n");
        return 1;
    }
    bool rebuild = false;
    bool dryRun = false;
    StringView targetId(u8"host");
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--rebuild") == 0)
        {
            rebuild = true;
        }
        else if (std::strcmp(argv[i], "--dry-run") == 0)
        {
            dryRun = true;
        }
        else if (std::strcmp(argv[i], "--target") == 0)
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "--target needs an id (e.g. web-astc)\n");
                return 1;
            }
            targetId = StringView(reinterpret_cast<const utf8char*>(argv[++i]));
        }
        else
        {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&consoleSink);

    const StringView projectDir(reinterpret_cast<const utf8char*>(argv[1]));
    UniquePtr<editor::EditorProject> project =
        editor::EditorProject::Open(projectDir);
    if (!project)
    {
        std::fprintf(stderr, "Tools.Cook: failed to open project '%s'\n", argv[1]);
        return 1;
    }

    pipeline::BuilderRegistry registry;
    pipeline::RegisterPipelineTypes(); // every asset/product/resource type + script cooks
    pipeline::RegisterAllBuilders(registry);

    vfs::NativeFileSystem sourcesMount(project->SourcesRoot().AsView());
    vfs::NativeFileSystem cacheMount(project->CacheRoot().AsView());
    JobSystem jobs;

    pipeline::CookProgress progress;
    progress.onItem = [](usize done, usize total, StringView path, bool ok)
    {
        std::printf("[%zu/%zu] %s %.*s\n", done, total, ok ? "ok  " : "FAIL",
                    static_cast<int>(path.Size()), reinterpret_cast<const char*>(path.Data()));
    };

    const bool hostOnly = (targetId == StringView(u8"host"));

    // The host cook is always run: it is the editor's DB AND the copy-forward source for any target.
    pipeline::CookDriver host(project->SourceDb(), project->CookedDb(), registry, &sourcesMount,
                              &cacheMount, &jobs);
    pipeline::CookPlan hostPlan = host.Plan(rebuild);
    std::printf("cook plan (host): %zu dirty, %zu up to date, %zu orphan(s), %zu without builders\n",
                hostPlan.dirty.Size(), hostPlan.upToDate, hostPlan.orphans.Size(),
                hostPlan.unbuildable);
    if (dryRun && hostOnly)
    {
        for (const pipeline::CookItem& item : hostPlan.dirty)
        {
            std::printf("  dirty: %.*s\n", static_cast<int>(item.path.Size()),
                        reinterpret_cast<const char*>(item.path.Data()));
        }
        return 0;
    }
    const pipeline::CookStats hostStats = host.Execute(hostPlan, &progress);
    std::printf("host cooked %zu, failed %zu, swept %zu orphan(s)\n", hostStats.cooked,
                hostStats.failed, hostStats.orphansSwept);

    usize totalFailed = hostStats.failed;

    if (!hostOnly)
    {
        // Materialize the per-target DB lazily. It roots at a SIBLING Cooked-<id>/ (NOT Cooked/<id>/):
        // the desktop pack walks Cooked/ recursively, so a nested target subtree would leak into the
        // desktop Content.pak (Fable ruling Q2 - "desktop byte-identical" must be structural). Its
        // cook.db lives under .cache/<id>/.
        String cookedDir(project->Directory());
        cookedDir.Append(u8"/Cooked-");
        cookedDir.Append(targetId);
        const String cacheDir = PathJoin(PathJoin(project->Directory(), u8".cache"), targetId);
        (void)CreateDirectory(cookedDir.AsView());
        (void)CreateDirectory(PathJoin(project->Directory(), u8".cache").AsView());
        (void)CreateDirectory(cacheDir.AsView());

        vfs::NativeFileSystem targetCookedMount(cookedDir.AsView());
        vfs::NativeFileSystem targetCacheMount(cacheDir.AsView());
        foundation::content::ContentDatabase targetDb(targetCookedMount, BinarySerializerFactory(),
                                                      u8".rasset");

        const pipeline::CookStats targetStats = pipeline::CookForTarget(
            project->SourceDb(), targetDb, project->CookedDb(), host.Db(), registry, &sourcesMount,
            &targetCacheMount, pipeline::CookTargetFor(targetId), &jobs, rebuild);
        std::printf("target '%.*s' cooked %zu, copied-forward %zu, failed %zu\n",
                    static_cast<int>(targetId.Size()),
                    reinterpret_cast<const char*>(targetId.Data()), targetStats.cooked,
                    targetStats.copiedForward, targetStats.failed);
        totalFailed += targetStats.failed;
    }

    GlobalLogger().RemoveSink(&consoleSink);
    return static_cast<int>(totalFailed);
}
