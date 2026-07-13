// RaptorExport - packages a project into a shippable dist. Thin CLI over
// draconic::editor::ExportProject (the editor's Export menu calls the same library; see
// docs/design/roadmap.md "Milestone: MVP-to-Export").
//
// Usage: RaptorExport <projectDirectory> <outDirectory> [--rebuild]
//
// The result runs with `RaptorPlayer <outDirectory>` - and links ZERO editor code.

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.scene;
import draconic.scene.resource;
import draconic.editor;
import draconic.editor.core;
import draconic.texture.editor;
import draconic.image.editor;
import draconic.image.resource;
import draconic.geometry.editor;
import draconic.animation.editor;
import draconic.materials.editor;
import draconic.shaders.editor;
import draconic.particles.editor;
import draconic.modelimporter;

using namespace draconic::core;
namespace ed = draconic::editor;
namespace dscene = draconic::scene;

namespace
{
    template <typename T>
    void Add(ed::BuilderRegistry& registry)
    {
        registry.Register(UniquePtr<ed::IAssetBuilder>(DefaultAllocator().New<T>(), DefaultAllocator()));
    }

    // Same builder set as RaptorCook/RaptorEditor (kept in lockstep).
    void RegisterAllBuilders(ed::BuilderRegistry& registry)
    {
        draconic::texture::RegisterTextureAsset();
        draconic::image::RegisterImageAsset();
        draconic::geometry::RegisterMeshAssets();
        draconic::animation::RegisterAnimationAssets();
        draconic::materials::RegisterMaterialAsset();
        draconic::shaders::RegisterShaderAsset();
        draconic::particles::RegisterParticleEffectAsset();
        draconic::modelimporter::RegisterModelManifestAsset();
        draconic::modelimporter::RegisterModelImporterTypes();
        draconic::image::RegisterImageResource();
        GlobalTypeRegistry().Register(dscene::SceneDocument::StaticType());
        RegisterSerializable<dscene::SceneDocument>();

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
        Add<draconic::modelimporter::ModelManifestAssetBuilder>(registry);
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: RaptorExport <projectDirectory> <outDirectory> [--rebuild]\n");
        return 1;
    }
    bool rebuild = false;
    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--rebuild") == 0) { rebuild = true; }
        else { std::fprintf(stderr, "unknown option: %s\n", argv[i]); return 1; }
    }

    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&consoleSink);

    const StringView projectDir(reinterpret_cast<const utf8char*>(argv[1]));
    const StringView outDir(reinterpret_cast<const utf8char*>(argv[2]));
    UniquePtr<ed::EditorProject> project = ed::EditorProject::Open(projectDir);
    if (!project)
    {
        std::fprintf(stderr, "RaptorExport: failed to open project '%s'\n", argv[1]);
        return 1;
    }

    ed::BuilderRegistry registry;
    RegisterAllBuilders(registry);

    ed::ExportStats stats;
    if (!ed::ExportProject(*project, outDir, registry, rebuild, &stats).IsOk())
    {
        std::fprintf(stderr, "RaptorExport: export failed (see log)\n");
        return 1;
    }
    std::printf("cook: %zu cooked, %zu failed | staged %zu scene(s) | packed %zu file(s)\n",
                stats.cooked, stats.cookFailed, stats.scenesStaged, stats.filesPacked);

    // Stage the player executable when it was built alongside this tool.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        // argv[0] can be bare/relative (running from inside the Export dir) - resolve it.
        const fs::path self = fs::weakly_canonical(fs::absolute(fs::path(argv[0]), ec), ec);
        const fs::path player = self.parent_path().parent_path() / "Player" / "RaptorPlayer";
        if (fs::exists(player, ec))
        {
            const fs::path target = fs::path(reinterpret_cast<const char*>(String(outDir).CStr())) / "RaptorPlayer";
            (void)fs::copy_file(player, target, fs::copy_options::overwrite_existing, ec);
            if (!ec) { std::printf("staged RaptorPlayer\n"); }
        }
        else
        {
            std::printf("note: RaptorPlayer not found next to RaptorExport - copy it manually\n");
        }
    }

    GlobalLogger().RemoveSink(&consoleSink);
    std::printf("export done: %s\n", reinterpret_cast<const char*>(String(outDir).CStr()));
    return 0;
}
