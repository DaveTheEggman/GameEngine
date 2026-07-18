// RaptorExport - packages a project into shippable dist(s). A thin CLI over the export DRIVER in
// draconic::editor (the editor's Export menu calls the same ExportOne/ExportAll; tests drive it
// headlessly). The whole dist - content (Content.pak + player.xml) AND the player + its runtime
// sidecars - comes from an export preset resolving to an export template, so a preset produces the
// same result whichever surface triggers it. Links ZERO editor code into the result.
//
// Usage:
//   RaptorExport <projectDir> [--out <dir>] [--preset <name> | --all] [--rebuild]
//   RaptorExport --template list
//   RaptorExport --template import <templateDir>
//
// No export_presets.xml in the project => a host preset for the current platform is synthesized, so a
// quick dev export works out of the box (the host template = the player next to this tool).

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
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
import draconic.input;
import draconic.input.resource;
import draconic.input.editor;
import draconic.modelimporter;
import draconic.render.subsystem;
import draconic.animation.subsystem;
import draconic.particles.subsystem;
import draconic.physics;
import draconic.physics.resource;
import draconic.physics.editor;
import draconic.ui.resource;
import draconic.ui.editor;
import draconic.physics.subsystem;

using namespace draconic::core;
namespace ed = draconic::editor;
namespace dscene = draconic::scene;
namespace vfs = draconic::vfs;
namespace fs = std::filesystem;

namespace
{
    [[nodiscard]] StringView Sv(const char* s) { return StringView(reinterpret_cast<const utf8char*>(s)); }
    // Takes const String& (not StringView): a StringView argument binds an implicit String temporary to
    // this reference parameter, which lives to the end of the full-expression - so the returned CStr()
    // is valid for the enclosing printf. (A by-value StringView would return a dangling pointer.)
    [[nodiscard]] const char* Cs(const String& s) { return reinterpret_cast<const char*>(s.CStr()); }

    template <typename T>
    void Add(ed::BuilderRegistry& registry)
    {
        registry.Register(UniquePtr<ed::IAssetBuilder>(DefaultAllocator().New<T>(), DefaultAllocator()));
    }

    // Same manager set the subsystems inject into every scene (kept in lockstep, like the
    // builder set below): the scene-stream transcode must know EVERY serializable component
    // type, or staged scenes would silently drop records. Managers are plain value pools -
    // no device, no subsystem lifecycle needed.
    void AddAllSceneManagers(dscene::Scene& scene)
    {
        namespace render = draconic::render;
        namespace anim = draconic::animation;
        namespace particles = draconic::particles;
        scene.AddSystem<render::MeshComponentManager>();
        scene.AddSystem<render::InstancedMeshComponentManager>();
        scene.AddSystem<render::SpriteComponentManager>();
        scene.AddSystem<render::DecalComponentManager>();
        scene.AddSystem<render::CameraComponentManager>();
        scene.AddSystem<render::LightComponentManager>();
        scene.AddSystem<render::ReflectionProbeComponentManager>();
        scene.AddSystem<render::EnvironmentSystem>();
        scene.AddSystem<anim::AnimationGraphComponentManager>();
        scene.AddSystem<anim::SkeletalAnimationComponentManager>();
        scene.AddSystem<anim::InstancedSkinningManager>();
        scene.AddSystem<particles::ParticleEffectComponentManager>();
        namespace physics = draconic::physics;
        scene.AddSystem<physics::RigidBodyComponentManager>();
        scene.AddSystem<physics::ColliderComponentManager>();
        scene.AddSystem<physics::JointComponentManager>();
        scene.AddSystem<physics::CharacterComponentManager>();
        scene.AddSystem<physics::PhysicsSceneSystem>();   // carries the settings block
    }

    // Pre-transcode every scene/prefab TEXT source stream to the binary wire (the editor
    // does the same on its main thread before the pack job) - the staged pak then matches
    // an editor export exactly. A stream that fails to transcode stages verbatim (the
    // runtime sniffs), so this can only improve the output.
    void CollectSceneStreams(draconic::content::Group& group, HashMap<Guid, Array<byte>>& out)
    {
        for (draconic::content::Instance* instance : group.Instances())
        {
            const bool isScene = instance->TypeName() == StringView(u8"SceneDocument");
            const bool isPrefab = instance->TypeName() == StringView(u8"PrefabDocument");
            if (!isScene && !isPrefab) { continue; }
            UniquePtr<IStream> stream = instance->ReadData(u8"scene");
            if (stream.Get() == nullptr) { continue; }
            dscene::Scene scratch(u8"__export_transcode");
            AddAllSceneManagers(scratch);
            Result<Array<byte>> bytes =
                dscene::TranscodeSceneStreamToBinary(*stream, scratch, /*includeSettings=*/isScene);
            if (bytes.HasValue()) { out.InsertOrAssign(instance->Id(), Move(bytes.Value())); }
        }
        for (draconic::content::Group* child : group.Groups()) { CollectSceneStreams(*child, out); }
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
        draconic::input::RegisterInputMapAsset();
        draconic::modelimporter::RegisterModelManifestAsset();
        draconic::model::RegisterModelResourceTypes();
        draconic::image::RegisterImageResource();
        draconic::physics::RegisterPhysicsAssets();
        draconic::physics::RegisterPhysicsResource();
        draconic::ui::RegisterUIAssets();
        draconic::ui::RegisterUIResource();
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
        Add<draconic::input::InputMapAssetBuilder>(registry);
        Add<draconic::modelimporter::ModelManifestAssetBuilder>(registry);
        Add<draconic::physics::CollisionShapeAssetBuilder>(registry);
        Add<draconic::physics::PhysicalMaterialAssetBuilder>(registry);
        Add<draconic::ui::UIDocumentAssetBuilder>(registry);
        Add<draconic::ui::UIThemeAssetBuilder>(registry);
    }

    // Directory containing this executable (Bin/... - where RaptorPlayer + its .runtime-libs live,
    // i.e. the host template's source). argv[0] can be bare/relative, so canonicalize it.
    [[nodiscard]] String ToolDir(const char* argv0)
    {
        std::error_code ec;
        const fs::path self = fs::weakly_canonical(fs::absolute(fs::path(argv0), ec), ec);
        const std::string dir = self.parent_path().string();
        return String(StringView(reinterpret_cast<const utf8char*>(dir.c_str())));
    }

    // Templates root: $DRACONIC_TEMPLATES_DIR or <user-data-dir>/templates (the CLI has no editor
    // settings, so it passes no override - same resolution the editor uses with an empty setting).
    [[nodiscard]] String TemplatesRoot() { return ed::ResolveTemplatesRoot(); }

    // Build the registry from the templates root (if it exists) + the host template (from toolDir).
    void BuildRegistry(ed::TemplateRegistry& registry, StringView templatesRoot, StringView toolDir)
    {
        std::error_code ec;
        UniquePtr<vfs::NativeFileSystem> rootFs;
        if (fs::is_directory(Cs(templatesRoot), ec))
        {
            rootFs = MakeUnique<vfs::NativeFileSystem>(DefaultAllocator(), templatesRoot);
        }
        vfs::NativeFileSystem toolFs(toolDir);
        registry.Refresh(templatesRoot, rootFs.Get(), toolDir, &toolFs);   // reads runtime-libs synchronously
    }

    int Usage()
    {
        std::fprintf(stderr,
            "usage:\n"
            "  RaptorExport <projectDir> [--out <dir>] [--preset <name> | --all] [--rebuild]\n"
            "  RaptorExport --template list\n"
            "  RaptorExport --template import <templateDir>\n");
        return 1;
    }

    // --- template management ---------------------------------------------------------------------

    int TemplateList(const char* argv0)
    {
        ed::TemplateRegistry registry;
        const String toolDir = ToolDir(argv0);
        const String root = TemplatesRoot();
        BuildRegistry(registry, root.AsView(), toolDir.AsView());
        std::printf("export templates (root: %s):\n", Cs(root.AsView()));
        for (usize i = 0; i < registry.Count(); ++i)
        {
            const ed::ExportTemplate* t = registry.At(i);
            std::printf("  %-24s %-8s %s%s\n", Cs(t->id.AsView()), Cs(t->platform.AsView()),
                        Cs(t->name.AsView()), t->isHost ? "  [host]" : "");
        }
        return 0;
    }

    int TemplateImport(const char* argv0, const char* srcDir)
    {
        const String root = TemplatesRoot();
        String importedId;
        if (!ed::ImportTemplate(Sv(srcDir), root.AsView(), &importedId).IsOk())
        {
            std::fprintf(stderr, "RaptorExport: failed to import '%s' (no valid template.xml?)\n", srcDir);
            return 1;
        }
        const String dst = PathJoin(root.AsView(), importedId.AsView());
        std::printf("imported template '%s' -> %s\n", Cs(importedId), Cs(dst));
        (void)argv0;
        return 0;
    }
}

int main(int argc, char** argv)
{
    ConsoleSink consoleSink;
    GlobalLogger().AddSink(&consoleSink);

    // --- template mode ---
    if (argc >= 2 && std::strcmp(argv[1], "--template") == 0)
    {
        if (argc < 3) { return Usage(); }
        if (std::strcmp(argv[2], "list") == 0) { return TemplateList(argv[0]); }
        if (std::strcmp(argv[2], "import") == 0)
        {
            if (argc < 4) { return Usage(); }
            return TemplateImport(argv[0], argv[3]);
        }
        return Usage();
    }

    // --- export mode ---
    if (argc < 2) { return Usage(); }
    const char* projectDir = argv[1];
    const char* outArg = nullptr;
    const char* presetName = nullptr;
    bool all = false;
    bool rebuild = false;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc)         { outArg = argv[++i]; }
        else if (std::strcmp(argv[i], "--preset") == 0 && i + 1 < argc) { presetName = argv[++i]; }
        else if (std::strcmp(argv[i], "--all") == 0)                    { all = true; }
        else if (std::strcmp(argv[i], "--rebuild") == 0)               { rebuild = true; }
        else { std::fprintf(stderr, "unknown option: %s\n", argv[i]); return Usage(); }
    }
    if (all && presetName != nullptr) { std::fprintf(stderr, "--all and --preset are mutually exclusive\n"); return 1; }

    UniquePtr<ed::EditorProject> project = ed::EditorProject::Open(Sv(projectDir));
    if (!project) { std::fprintf(stderr, "RaptorExport: failed to open project '%s'\n", projectDir); return 1; }

    ed::BuilderRegistry builders;
    RegisterAllBuilders(builders);

    // Component reflection (data-version gates) before any scene stream deserializes.
    draconic::render::RegisterRenderComponentReflection();
    draconic::animation::RegisterAnimationComponentReflection();
    draconic::particles::RegisterParticleComponentReflection();
    draconic::physics::RegisterPhysicsComponentReflection();
    HashMap<Guid, Array<byte>> sceneStreams;
    CollectSceneStreams(*project->SourceDb().RootGroup(), sceneStreams);

    // Presets: from <project>/export_presets.xml, else a synthesized host preset.
    ed::ExportPresetSet presets;
    {
        vfs::NativeFileSystem projectFs(project->Directory());
        if (!ed::LoadExportPresets(projectFs, presets).IsOk()) { ed::DefaultExportPresets(presets); }
    }

    ed::TemplateRegistry registry;
    BuildRegistry(registry, TemplatesRoot().AsView(), ToolDir(argv[0]).AsView());

    const String outRoot = (outArg != nullptr) ? String(Sv(outArg))
                                               : PathJoin(project->Directory(), u8"Dist");

    if (all)
    {
        const Span<const ed::ExportPreset> span(presets.presets.Data(), presets.presets.Size());
        if (!ed::ExportAll(*project, span, registry, builders, outRoot.AsView(), rebuild, {}, true,
                           &sceneStreams).IsOk())
        {
            std::fprintf(stderr, "RaptorExport: one or more presets failed (see log)\n");
            return 1;
        }
        std::printf("export done: %zu preset(s) -> %s\n", presets.presets.Size(), Cs(outRoot.AsView()));
        return 0;
    }

    const ed::ExportPreset* preset = (presetName != nullptr)
        ? presets.Find(Sv(presetName))
        : (presets.presets.Size() > 0 ? &presets.presets[0] : nullptr);
    if (preset == nullptr)
    {
        std::fprintf(stderr, "RaptorExport: no preset%s%s\n",
                     presetName ? " named " : "", presetName ? presetName : " defined");
        return 1;
    }

    ed::ExportResult result;
    if (!ed::ExportOne(*project, *preset, registry, builders, outRoot.AsView(), rebuild, &result, {}, true,
                       &sceneStreams).IsOk())
    {
        std::fprintf(stderr, "RaptorExport: export failed (see log)\n");
        return 1;
    }
    std::printf("exported '%s' -> %s\n  cook: %zu cooked, %zu scene(s), %zu packed | staged %zu file(s)\n",
                Cs(preset->name.AsView()), Cs(result.outputDir.AsView()),
                result.content.cooked, result.content.scenesStaged, result.content.filesPacked,
                result.filesStaged);
    if (!result.engineVersionWarning.IsEmpty())
    {
        std::printf("  warning: %s\n", Cs(result.engineVersionWarning.AsView()));
    }

    GlobalLogger().RemoveSink(&consoleSink);
    return 0;
}
