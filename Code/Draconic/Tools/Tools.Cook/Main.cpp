// Draconic.Tools.Cook - the command-line cooker (docs/design/asset-pipeline.md §6). Headless: opens the
// project, registers every builder, plans + executes the incremental cook.
//
// Usage: Draconic.Tools.Cook <projectDirectory> [--rebuild] [--dry-run]
//   --rebuild   force-cook every buildable asset (the "forgot the version bump" big hammer)
//   --dry-run   print the plan (dirty set + orphans) without cooking
// Exit code = number of failed cooks (0 = success).

#include <cstdio>
#include <cstring>
#include "Core/Log/Log.h"

import foundation.core;
import foundation.animation;
import foundation.particles;
import foundation.content;
import foundation.vfs;
import pipeline.core;
import editor.core;
import pipeline.cook;
import texture.pipeline;
import fonts.pipeline;
import image.pipeline;
import foundation.image.resource;
import geometry.pipeline;
import animation.pipeline;
import materials.pipeline;
import shaders.pipeline;
import particles.pipeline;
import foundation.input;
import foundation.input.resource;
import input.pipeline;
import modelimporter;
import foundation.physics;
import foundation.physics.resource;
import physics.pipeline;
import foundation.ui.resource;
import ui.pipeline;
import foundation.audio;
import foundation.audio.resource;
import audio.pipeline;
import foundation.script;
import foundation.script.wren;
import foundation.script.angelscript;
import script.wren.pipeline;
import script.angelscript.pipeline;
import foundation.script.resource;
import script.pipeline;

using namespace foundation::core;
namespace vfs = foundation::vfs;

namespace
{
    template <typename T>
    void Add(pipeline::BuilderRegistry& registry)
    {
        registry.Register(
            UniquePtr<pipeline::IAssetBuilder>(DefaultAllocator().New<T>(), DefaultAllocator()));
    }

    // Every builder the engine ships (the editor executable assembles the same set).
    void RegisterAllBuilders(pipeline::BuilderRegistry& registry)
    {
        pipeline::RegisterAssetReflection(); // base Asset::fileName + SourcePath
        pipeline::RegisterTextureAsset();
        pipeline::RegisterFontAsset(); // asset + FontResource product
        pipeline::RegisterImageAsset();
        pipeline::RegisterMeshAssets();
        pipeline::RegisterAnimationAssets();
        pipeline::RegisterMaterialAsset();
        pipeline::RegisterShaderAsset();
        pipeline::RegisterParticleEffectAsset();
        pipeline::RegisterInputMapAsset();
        pipeline::RegisterModelManifestAsset();
        // Product/resource types: ReadObject constructs cooked products BY TYPE NAME, so the
        // runtime-facing types must be registered too (meshes/materials/textures/animation/
        // manifest via the model-importer helper, plus the image resource).
        foundation::model::RegisterModelResourceTypes();
        foundation::image::RegisterImageResource();
        pipeline::RegisterPhysicsAssets();
        foundation::physics::RegisterPhysicsResource();
        pipeline::RegisterUIAssets();
        foundation::ui::RegisterUIResource();
        pipeline::RegisterAudioAssets();
        foundation::audio::RegisterAudioResource();
        pipeline::RegisterScriptAssets();
        foundation::script::RegisterScriptResource();
        // The builder resolves a per-language COOK through the registry (B3);
        // registering backends + cooks is the entry point's job - both languages.
        foundation::script::wren::RegisterWrenScriptBackend();
        foundation::script::angelscript::RegisterAngelScriptBackend();
        pipeline::RegisterWrenScriptCook();
        pipeline::RegisterAngelScriptScriptCook();

        Add<pipeline::TextureAssetBuilder>(registry);
        Add<pipeline::FontAssetBuilder>(registry);
        Add<pipeline::ImageAssetBuilder>(registry);
        Add<pipeline::StaticMeshAssetBuilder>(registry);
        Add<pipeline::SkinnedMeshAssetBuilder>(registry);
        Add<pipeline::SkeletonAssetBuilder>(registry);
        Add<pipeline::AnimationClipAssetBuilder>(registry);
        Add<pipeline::AnimationGraphAssetBuilder>(registry);
        Add<pipeline::MaterialAssetBuilder>(registry);
        Add<pipeline::ShaderAssetBuilder>(registry);
        Add<pipeline::ParticleEffectAssetBuilder>(registry);
        Add<pipeline::InputMapAssetBuilder>(registry);
        Add<pipeline::ModelManifestAssetBuilder>(registry);
        Add<pipeline::CollisionShapeAssetBuilder>(registry);
        Add<pipeline::PhysicalMaterialAssetBuilder>(registry);
        Add<pipeline::UIDocumentAssetBuilder>(registry);
        Add<pipeline::UIThemeAssetBuilder>(registry);
        Add<pipeline::AudioClipAssetBuilder>(registry);
        Add<pipeline::AudioBusLayoutAssetBuilder>(registry);
        Add<pipeline::SoundCueAssetBuilder>(registry);
        Add<pipeline::ScriptClassAssetBuilder>(registry);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: Draconic.Tools.Cook <projectDirectory> [--rebuild] [--dry-run]\n");
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
        std::fprintf(stderr, "Draconic.Tools.Cook: failed to open project '%s'\n", argv[1]);
        return 1;
    }

    pipeline::BuilderRegistry registry;
    RegisterAllBuilders(registry);

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
