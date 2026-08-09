// Tools.Cook - the command-line cooker (docs/design/asset-pipeline.md §6). Headless: opens the
// project, registers every builder, plans + executes the incremental cook.
//
// Usage: Tools.Cook <projectDirectory> [--rebuild] [--dry-run]
//   --rebuild   force-cook every buildable asset (the "forgot the version bump" big hammer)
//   --dry-run   print the plan (dirty set + orphans) without cooking
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

    pipeline::CookDriver driver(project->SourceDb(), project->CookedDb(), registry, &sourcesMount,
                              &cacheMount, &jobs);

    pipeline::CookPlan plan = driver.Plan(rebuild);
    std::printf("cook plan: %zu dirty, %zu up to date, %zu orphan(s), %zu without builders\n",
                plan.dirty.Size(), plan.upToDate, plan.orphans.Size(), plan.unbuildable);
    if (dryRun)
    {
        for (const pipeline::CookItem& item : plan.dirty)
        {
            std::printf("  dirty: %.*s\n", static_cast<int>(item.path.Size()),
                        reinterpret_cast<const char*>(item.path.Data()));
        }
        return 0;
    }

    pipeline::CookProgress progress;
    progress.onItem = [](usize done, usize total, StringView path, bool ok)
    {
        std::printf("[%zu/%zu] %s %.*s\n", done, total, ok ? "ok  " : "FAIL",
                    static_cast<int>(path.Size()), reinterpret_cast<const char*>(path.Data()));
    };
    const pipeline::CookStats stats = driver.Execute(plan, &progress);
    std::printf("cooked %zu, failed %zu, swept %zu orphan(s)\n", stats.cooked, stats.failed,
                stats.orphansSwept);

    GlobalLogger().RemoveSink(&consoleSink);
    return static_cast<int>(stats.failed);
}
