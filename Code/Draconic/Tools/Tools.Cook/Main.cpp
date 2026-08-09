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

import draconic.core;
import draconic.animation;
import draconic.particles;
import draconic.content;
import draconic.vfs;
import draconic.pipeline.core;
import draconic.editor.core;
import draconic.pipeline.cook;
import draconic.texture.pipeline;
import draconic.fonts.pipeline;
import draconic.image.pipeline;
import draconic.image.resource;
import draconic.geometry.pipeline;
import draconic.animation.pipeline;
import draconic.materials.pipeline;
import draconic.shaders.pipeline;
import draconic.particles.pipeline;
import draconic.input;
import draconic.input.resource;
import draconic.input.pipeline;
import draconic.modelimporter;
import draconic.physics;
import draconic.physics.resource;
import draconic.physics.pipeline;
import draconic.ui.resource;
import draconic.ui.pipeline;
import draconic.audio;
import draconic.audio.resource;
import draconic.audio.pipeline;
import draconic.script;
import draconic.script.wren;
import draconic.script.angelscript;
import draconic.script.wren.pipeline;
import draconic.script.angelscript.pipeline;
import draconic.script.resource;
import draconic.script.pipeline;

using namespace draconic::core;
namespace editor = draconic::editor;
namespace vfs = draconic::vfs;

namespace
{
    template <typename T>
    void Add(draconic::pipeline::BuilderRegistry& registry)
    {
        registry.Register(
            UniquePtr<draconic::pipeline::IAssetBuilder>(DefaultAllocator().New<T>(), DefaultAllocator()));
    }

    // Every builder the engine ships (the editor executable assembles the same set).
    void RegisterAllBuilders(draconic::pipeline::BuilderRegistry& registry)
    {
        draconic::pipeline::RegisterAssetReflection(); // base Asset::fileName + SourcePath
        draconic::pipeline::RegisterTextureAsset();
        draconic::pipeline::RegisterFontAsset(); // asset + FontResource product
        draconic::pipeline::RegisterImageAsset();
        draconic::pipeline::RegisterMeshAssets();
        draconic::pipeline::RegisterAnimationAssets();
        draconic::pipeline::RegisterMaterialAsset();
        draconic::pipeline::RegisterShaderAsset();
        draconic::pipeline::RegisterParticleEffectAsset();
        draconic::pipeline::RegisterInputMapAsset();
        draconic::pipeline::RegisterModelManifestAsset();
        // Product/resource types: ReadObject constructs cooked products BY TYPE NAME, so the
        // runtime-facing types must be registered too (meshes/materials/textures/animation/
        // manifest via the model-importer helper, plus the image resource).
        draconic::model::RegisterModelResourceTypes();
        draconic::image::RegisterImageResource();
        draconic::pipeline::RegisterPhysicsAssets();
        draconic::physics::RegisterPhysicsResource();
        draconic::pipeline::RegisterUIAssets();
        draconic::ui::RegisterUIResource();
        draconic::pipeline::RegisterAudioAssets();
        draconic::audio::RegisterAudioResource();
        draconic::pipeline::RegisterScriptAssets();
        draconic::script::RegisterScriptResource();
        // The builder resolves a per-language COOK through the registry (B3);
        // registering backends + cooks is the entry point's job - both languages.
        draconic::script::wren::RegisterWrenScriptBackend();
        draconic::script::angelscript::RegisterAngelScriptBackend();
        draconic::pipeline::RegisterWrenScriptCook();
        draconic::pipeline::RegisterAngelScriptScriptCook();

        Add<draconic::pipeline::TextureAssetBuilder>(registry);
        Add<draconic::pipeline::FontAssetBuilder>(registry);
        Add<draconic::pipeline::ImageAssetBuilder>(registry);
        Add<draconic::pipeline::StaticMeshAssetBuilder>(registry);
        Add<draconic::pipeline::SkinnedMeshAssetBuilder>(registry);
        Add<draconic::pipeline::SkeletonAssetBuilder>(registry);
        Add<draconic::pipeline::AnimationClipAssetBuilder>(registry);
        Add<draconic::pipeline::AnimationGraphAssetBuilder>(registry);
        Add<draconic::pipeline::MaterialAssetBuilder>(registry);
        Add<draconic::pipeline::ShaderAssetBuilder>(registry);
        Add<draconic::pipeline::ParticleEffectAssetBuilder>(registry);
        Add<draconic::pipeline::InputMapAssetBuilder>(registry);
        Add<draconic::pipeline::ModelManifestAssetBuilder>(registry);
        Add<draconic::pipeline::CollisionShapeAssetBuilder>(registry);
        Add<draconic::pipeline::PhysicalMaterialAssetBuilder>(registry);
        Add<draconic::pipeline::UIDocumentAssetBuilder>(registry);
        Add<draconic::pipeline::UIThemeAssetBuilder>(registry);
        Add<draconic::pipeline::AudioClipAssetBuilder>(registry);
        Add<draconic::pipeline::AudioBusLayoutAssetBuilder>(registry);
        Add<draconic::pipeline::SoundCueAssetBuilder>(registry);
        Add<draconic::pipeline::ScriptClassAssetBuilder>(registry);
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
    UniquePtr<draconic::editor::EditorProject> project =
        draconic::editor::EditorProject::Open(projectDir);
    if (!project)
    {
        std::fprintf(stderr, "Draconic.Tools.Cook: failed to open project '%s'\n", argv[1]);
        return 1;
    }

    draconic::pipeline::BuilderRegistry registry;
    RegisterAllBuilders(registry);

    vfs::NativeFileSystem sourcesMount(project->SourcesRoot().AsView());
    vfs::NativeFileSystem cacheMount(project->CacheRoot().AsView());
    JobSystem jobs;

    draconic::pipeline::CookDriver driver(project->SourceDb(), project->CookedDb(), registry, &sourcesMount,
                              &cacheMount, &jobs);

    draconic::pipeline::CookPlan plan = driver.Plan(rebuild);
    std::printf("cook plan: %zu dirty, %zu up to date, %zu orphan(s), %zu without builders\n",
                plan.dirty.Size(), plan.upToDate, plan.orphans.Size(), plan.unbuildable);
    if (dryRun)
    {
        for (const draconic::pipeline::CookItem& item : plan.dirty)
        {
            std::printf("  dirty: %.*s\n", static_cast<int>(item.path.Size()),
                        reinterpret_cast<const char*>(item.path.Data()));
        }
        return 0;
    }

    draconic::pipeline::CookProgress progress;
    progress.onItem = [](usize done, usize total, StringView path, bool ok)
    {
        std::printf("[%zu/%zu] %s %.*s\n", done, total, ok ? "ok  " : "FAIL",
                    static_cast<int>(path.Size()), reinterpret_cast<const char*>(path.Data()));
    };
    const draconic::pipeline::CookStats stats = driver.Execute(plan, &progress);
    std::printf("cooked %zu, failed %zu, swept %zu orphan(s)\n", stats.cooked, stats.failed,
                stats.orphansSwept);

    GlobalLogger().RemoveSink(&consoleSink);
    return static_cast<int>(stats.failed);
}
