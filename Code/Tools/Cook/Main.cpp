// RaptorCook - the command-line cooker (docs/design/asset-pipeline.md §6). Headless: opens the
// project, registers every builder, plans + executes the incremental cook.
//
// Usage: RaptorCook <projectDirectory> [--rebuild] [--dry-run]
//   --rebuild   force-cook every buildable asset (the "forgot the version bump" big hammer)
//   --dry-run   print the plan (dirty set + orphans) without cooking
// Exit code = number of failed cooks (0 = success).

#include <cstdio>
#include <cstring>
#include "Core/Log/Log.h"

import draconic.core;
import draconic.content;
import draconic.vfs;
import draconic.editor;
import draconic.editor.core;
import draconic.editor.cook;
import draconic.texture.editor;
import draconic.image.editor;
import draconic.image.resource;
import draconic.geometry.editor;
import draconic.animation.editor;
import draconic.materials.editor;
import draconic.shaders.editor;
import draconic.particles.editor;
import draconic.input;
import draconic.input.resource;
import draconic.input.editor;
import draconic.modelimporter;
import draconic.physics;
import draconic.physics.resource;
import draconic.physics.editor;
import draconic.ui.resource;
import draconic.ui.editor;
import draconic.audio;
import draconic.audio.resource;
import draconic.audio.editor;
import draconic.script;
import draconic.script.wren;
import draconic.script.resource;
import draconic.script.editor;

using namespace draconic::core;
namespace ed = draconic::editor;
namespace vfs = draconic::vfs;

namespace
{
    template <typename T>
    void Add(ed::BuilderRegistry& registry)
    {
        registry.Register(UniquePtr<ed::IAssetBuilder>(DefaultAllocator().New<T>(), DefaultAllocator()));
    }

    // Every builder the engine ships (the editor executable assembles the same set).
    void RegisterAllBuilders(ed::BuilderRegistry& registry)
    {
        draconic::texture::RegisterTextureAsset();
        draconic::image::RegisterImageAsset();
        draconic::geometry::RegisterMeshAssets();
        draconic::animation::RegisterAnimationAssets();
        draconic::materials::RegisterMaterialAsset();
        draconic::shaders::RegisterShaderAsset();
        draconic::particles::RegisterParticleEffectAsset();
        draconic::input::RegisterInputMapAsset();
        draconic::modelimporter::RegisterModelManifestAsset();
        // Product/resource types: ReadObject constructs cooked products BY TYPE NAME, so the
        // runtime-facing types must be registered too (meshes/materials/textures/animation/
        // manifest via the model-importer helper, plus the image resource).
        draconic::model::RegisterModelResourceTypes();
        draconic::image::RegisterImageResource();
        draconic::physics::RegisterPhysicsAssets();
        draconic::physics::RegisterPhysicsResource();
        draconic::ui::RegisterUIAssets();
        draconic::ui::RegisterUIResource();
        draconic::audio::RegisterAudioAssets();
        draconic::audio::RegisterAudioResource();
        draconic::script::RegisterScriptAssets();
        draconic::script::RegisterScriptResource();
        // The script builder resolves its harvest VM through the backend
        // REGISTRY (B3); registering backends is the entry point's job.
        draconic::script::wren::RegisterWrenScriptBackend();

        Add<draconic::texture::TextureAssetBuilder>(registry);
        Add<draconic::image::ImageAssetBuilder>(registry);
        Add<draconic::geometry::StaticMeshAssetBuilder>(registry);
        Add<draconic::geometry::SkinnedMeshAssetBuilder>(registry);
        Add<draconic::animation::SkeletonAssetBuilder>(registry);
        Add<draconic::animation::AnimationClipAssetBuilder>(registry);
        Add<draconic::animation::AnimationGraphAssetBuilder>(registry);
        Add<draconic::materials::MaterialAssetBuilder>(registry);
        Add<draconic::shaders::ShaderAssetBuilder>(registry);
        Add<draconic::particles::ParticleEffectAssetBuilder>(registry);
        Add<draconic::input::InputMapAssetBuilder>(registry);
        Add<draconic::modelimporter::ModelManifestAssetBuilder>(registry);
        Add<draconic::physics::CollisionShapeAssetBuilder>(registry);
        Add<draconic::physics::PhysicalMaterialAssetBuilder>(registry);
        Add<draconic::ui::UIDocumentAssetBuilder>(registry);
        Add<draconic::ui::UIThemeAssetBuilder>(registry);
        Add<draconic::audio::AudioClipAssetBuilder>(registry);
        Add<draconic::audio::AudioBusLayoutAssetBuilder>(registry);
        Add<draconic::audio::SoundCueAssetBuilder>(registry);
        Add<draconic::script::ScriptClassAssetBuilder>(registry);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: RaptorCook <projectDirectory> [--rebuild] [--dry-run]\n");
        return 1;
    }
    bool rebuild = false;
    bool dryRun = false;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--rebuild") == 0) { rebuild = true; }
        else if (std::strcmp(argv[i], "--dry-run") == 0) { dryRun = true; }
        else { std::fprintf(stderr, "unknown option: %s\n", argv[i]); return 1; }
    }

    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&consoleSink);

    const StringView projectDir(reinterpret_cast<const utf8char*>(argv[1]));
    UniquePtr<draconic::editor::EditorProject> project = draconic::editor::EditorProject::Open(projectDir);
    if (!project)
    {
        std::fprintf(stderr, "RaptorCook: failed to open project '%s'\n", argv[1]);
        return 1;
    }

    ed::BuilderRegistry registry;
    RegisterAllBuilders(registry);

    vfs::NativeFileSystem sourcesMount(project->SourcesRoot().AsView());
    vfs::NativeFileSystem cacheMount(project->CacheRoot().AsView());
    JobSystem jobs;

    ed::CookDriver driver(project->SourceDb(), project->CookedDb(), registry,
                          &sourcesMount, &cacheMount, &jobs);

    ed::CookPlan plan = driver.Plan(rebuild);
    std::printf("cook plan: %zu dirty, %zu up to date, %zu orphan(s), %zu without builders\n",
                plan.dirty.Size(), plan.upToDate, plan.orphans.Size(), plan.unbuildable);
    if (dryRun)
    {
        for (const ed::CookItem& item : plan.dirty)
        {
            std::printf("  dirty: %.*s\n", static_cast<int>(item.path.Size()),
                        reinterpret_cast<const char*>(item.path.Data()));
        }
        return 0;
    }

    ed::CookProgress progress;
    progress.onItem = [](usize done, usize total, StringView path, bool ok) {
        std::printf("[%zu/%zu] %s %.*s\n", done, total, ok ? "ok  " : "FAIL",
                    static_cast<int>(path.Size()), reinterpret_cast<const char*>(path.Data()));
    };
    const ed::CookStats stats = driver.Execute(plan, &progress);
    std::printf("cooked %zu, failed %zu, swept %zu orphan(s)\n",
                stats.cooked, stats.failed, stats.orphansSwept);

    GlobalLogger().RemoveSink(&consoleSink);
    return static_cast<int>(stats.failed);
}
