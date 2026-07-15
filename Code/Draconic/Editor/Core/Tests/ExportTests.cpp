// End-to-end export pipeline: author a project programmatically (scene + a cooked mesh asset
// + a resource ref between them + a game script), ExportProject it, then consume the dist the
// way RaptorPlayer does - ONE binary ContentDatabase over the pak for products AND scenes, the
// script as a raw pak entry, the manifest via the lean project helpers. Everything under the
// versioned-payload formats.
#include <atomic>   // gcc modules: pull in std::atomic bodies before this import mix
#include <doctest/doctest.h>
#include <filesystem>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.vfs.pak;
import draconic.content;
import draconic.resource;
import draconic.project;
import draconic.geometry;
import draconic.geometry.editor;
import draconic.geometry.resource;
import draconic.render.subsystem;
import draconic.scene;
import draconic.scene.resource;
import draconic.scene.editor;
import draconic.editor;
import draconic.editor.core;

using namespace draconic::core;
namespace ed = draconic::editor;
namespace proj = draconic::project;
namespace dscene = draconic::scene;
namespace geo = draconic::geometry;

namespace
{
    void NukeTree(StringView root)
    {
        std::filesystem::remove_all(
            std::filesystem::path(reinterpret_cast<const char*>(String(root).CStr())));
    }
}

TEST_CASE("export: project -> dist pak -> player-style load-back (versioned formats)")
{
    GlobalTypeRegistry().Register(dscene::SceneDocument::StaticType());
    RegisterSerializable<dscene::SceneDocument>();
    geo::RegisterMeshAssets();
    GlobalTypeRegistry().Register(geo::StaticMeshSource::StaticType());
    RegisterSerializable<geo::StaticMeshSource>();

    const StringView projectDir = u8"draconic_export_e2e_project";
    const StringView distDir = u8"draconic_export_e2e_dist";
    NukeTree(projectDir);
    NukeTree(distDir);

    Guid meshId;
    Guid sceneId;
    // --- author the project ---
    {
        REQUIRE(ed::EditorProject::Create(projectDir, u8"E2E").IsOk());
        UniquePtr<ed::EditorProject> project = ed::EditorProject::Open(projectDir);
        REQUIRE(static_cast<bool>(project));

        // A cooked-pipeline asset: a cube mesh (what the primitive creators produce).
        draconic::content::Group* meshes = project->SourceDb().RootGroup()->CreateGroup(u8"Meshes");
        draconic::content::Instance* meshAsset =
            meshes->CreateInstance(u8"Cube", geo::StaticMeshAsset::StaticType());
        REQUIRE(meshAsset != nullptr);
        geo::StaticMeshAsset asset;
        geo::MeshImporter::Import(*geo::Primitives::Cube(2.0f), asset);
        REQUIRE(meshAsset->WriteObject(asset).IsOk());
        meshId = meshAsset->Id();

        // A scene whose entity references the mesh by guid.
        draconic::content::Group* scenes = project->SourceDb().RootGroup()->CreateGroup(u8"Scenes");
        draconic::content::Instance* sceneInstance =
            scenes->CreateInstance(u8"Main", dscene::SceneDocument::StaticType());
        REQUIRE(sceneInstance != nullptr);
        dscene::SceneDocument doc;
        doc.name = String(u8"Main");
        REQUIRE(sceneInstance->WriteObject(doc).IsOk());
        sceneId = sceneInstance->Id();
        {
            dscene::Scene scene(u8"Main");
            scene.AddSystem<draconic::render::MeshComponentManager>();
            const dscene::EntityHandle e = scene.CreateEntity(u8"Box");
            draconic::render::MeshComponent& mc =
                scene.GetSystem<draconic::render::MeshComponentManager>()->Add(e);
            mc.mesh.SetId(meshId);
            REQUIRE(dscene::SaveScene(scene, *sceneInstance).IsOk());
        }

        // The game script + manifest wiring.
        {
            const StringView script = u8"class Game { construct new() {} }\n";
            draconic::vfs::NativeFileSystem root(projectDir);
            REQUIRE(root.AsWritable()->Save(u8"Scripts/game.wren",
                Span<const byte>(reinterpret_cast<const byte*>(script.Data()), script.Size())).IsOk());
        }
        project->Settings().defaultSceneId = sceneId;
        project->Settings().defaultScene = String(u8"Scenes/Main");
        project->Settings().startupScript = String(u8"Scripts/game.wren");
        REQUIRE(project->SaveSettings().IsOk());

        // --- export ---
        ed::BuilderRegistry registry;
        registry.Register(UniquePtr<ed::IAssetBuilder>(
            DefaultAllocator().New<geo::StaticMeshAssetBuilder>(), DefaultAllocator()));
        ed::ExportStats stats;
        REQUIRE(ed::ExportProject(*project, distDir, registry, false, &stats).IsOk());
        CHECK(stats.cooked == 1u);        // the cube
        CHECK(stats.scenesStaged == 1u);
        CHECK(stats.filesPacked >= 3u);   // product + scene envelope + scene stream + script
    }

    // --- consume the dist exactly like RaptorPlayer's dist mode ---
    draconic::vfs::NativeFileSystem distRoot(distDir);
    proj::ProjectSettings manifest;
    REQUIRE(proj::LoadProjectSettings(distRoot, manifest, proj::kDistManifestFile).IsOk());
    CHECK(manifest.defaultScene == u8"Scenes/Main");
    CHECK(manifest.defaultSceneId == sceneId);   // dist manifest carries the guid too
    CHECK(manifest.startupScript == u8"Scripts/game.wren");

    draconic::vfs::PakFileSystem pak(PathJoin(distDir, proj::kDistContentPak).AsView());
    REQUIRE(pak.IsValid());
    draconic::content::ContentDatabase db(pak, BinarySerializerFactory(), proj::kCookedAssetExtension);

    // The scene loads from the pak under its ORIGINAL guid/path, and its mesh ref resolves
    // against the pak-hosted product through the CPU mesh factory.
    // Resolve by guid (the player's primary path), then confirm the path mirror agrees.
    draconic::content::Instance* sceneInstance = db.GetInstance(manifest.defaultSceneId);
    REQUIRE(sceneInstance != nullptr);
    CHECK(sceneInstance->Id() == sceneId);
    CHECK(db.GetInstance(manifest.defaultScene.AsView()) == sceneInstance);

    dscene::Scene scene;
    auto* meshes = scene.AddSystem<draconic::render::MeshComponentManager>();
    REQUIRE(dscene::LoadScene(*sceneInstance, scene).IsOk());
    draconic::resource::ResourceManager resources(db);
    geo::StaticMeshFactory meshFactory;
    resources.AddFactory(&meshFactory);
    dscene::ResolveSceneResources(scene, resources);

    draconic::render::MeshComponent* mc = nullptr;
    meshes->ForEach([&](draconic::render::MeshComponent& c, dscene::EntityHandle) { mc = &c; });
    REQUIRE(mc != nullptr);
    CHECK(mc->mesh.id == meshId);
    geo::StaticMesh* mesh = mc->mesh.Get();
    REQUIRE(mesh != nullptr);
    CHECK(mesh->bounds.max.x == doctest::Approx(1.0f));   // the 2.0 cube

    // The game script rides in the pak as a raw entry.
    UniquePtr<IStream> script = pak.Open(manifest.startupScript.AsView(), FileMode::Read);
    REQUIRE(script);
    CHECK(script->Size() > 0);

    NukeTree(projectDir);
    NukeTree(distDir);
}

TEST_CASE("export: preset set round-trips through export_presets.xml")
{
    const String dir = PathJoin(
        StringView(reinterpret_cast<const utf8char*>(
            std::filesystem::temp_directory_path().string().c_str())),
        u8"draconic_presets_test");
    NukeTree(dir.AsView());
    REQUIRE(CreateDirectory(dir.AsView()));
    draconic::vfs::NativeFileSystem root(dir.AsView());

    // Absent file => NotFound, so callers know to fall back to defaults.
    {
        ed::ExportPresetSet loaded;
        CHECK_FALSE(ed::LoadExportPresets(root, loaded).IsOk());
    }

    // The built-in default names the host platform and leaves templateId blank (resolve by platform).
    ed::ExportPresetSet defaults;
    ed::DefaultExportPresets(defaults);
    REQUIRE(defaults.presets.Size() == 1u);
    CHECK(defaults.presets[0].platform == GetHostPlatformName());
    CHECK(defaults.presets[0].templateId.IsEmpty());

    // Author a two-preset set (one blank-template, one explicit-template with extra files) and save it.
    ed::ExportPresetSet out;
    ed::ExportPreset a;
    a.name = String(u8"Linux Desktop"); a.platform = String(u8"Linux64"); a.outputSubdir = String(u8"Linux64");
    ed::ExportPreset b;
    b.name = String(u8"Windows Desktop"); b.platform = String(u8"Win64");
    b.templateId = String(u8"raptor-win64-0.1.0"); b.playerName = String(u8"MyGame.exe");
    b.outputSubdir = String(u8"Win64");
    b.additionalFiles.PushBack(String(u8"icon.ico"));
    b.additionalFiles.PushBack(String(u8"config.xml"));
    out.presets.PushBack(Move(a));
    out.presets.PushBack(Move(b));
    REQUIRE(ed::SaveExportPresets(*root.AsWritable(), out).IsOk());

    // Load back and check every field survived, including the additionalFiles array + lookup.
    ed::ExportPresetSet loaded;
    REQUIRE(ed::LoadExportPresets(root, loaded).IsOk());
    REQUIRE(loaded.presets.Size() == 2u);
    CHECK(loaded.presets[0].name == u8"Linux Desktop");
    CHECK(loaded.presets[0].templateId.IsEmpty());
    CHECK(loaded.presets[0].playerName.IsEmpty());

    const ed::ExportPreset* win = loaded.Find(u8"Windows Desktop");
    REQUIRE(win != nullptr);
    CHECK(win->platform == u8"Win64");
    CHECK(win->templateId == u8"raptor-win64-0.1.0");
    CHECK(win->playerName == u8"MyGame.exe");
    REQUIRE(win->additionalFiles.Size() == 2u);
    CHECK(win->additionalFiles[0] == u8"icon.ico");
    CHECK(win->additionalFiles[1] == u8"config.xml");
    CHECK(loaded.Find(u8"nope") == nullptr);

    NukeTree(dir.AsView());
}

namespace
{
    String TempDir(StringView leaf)
    {
        return PathJoin(StringView(reinterpret_cast<const utf8char*>(
                            std::filesystem::temp_directory_path().string().c_str())), leaf);
    }
    void SaveText(draconic::vfs::NativeFileSystem& fs, StringView name, StringView text)
    {
        (void)fs.AsWritable()->Save(name, Span<const byte>(
            reinterpret_cast<const byte*>(text.Data()), text.Size()));
    }
}

TEST_CASE("export: template.xml round-trips + host synthesis reads its runtime-libs")
{
    const String dir = TempDir(u8"draconic_template_test");
    NukeTree(dir.AsView());
    REQUIRE(CreateDirectory(dir.AsView()));
    draconic::vfs::NativeFileSystem root(dir.AsView());

    // template.xml round-trip.
    ed::ExportTemplate t;
    t.id = String(u8"raptor-win64-0.1.0"); t.name = String(u8"Windows Desktop 0.1.0");
    t.platform = String(u8"Win64"); t.engineVersion = String(u8"0.1.0");
    t.playerBinary = String(u8"RaptorPlayer.exe");
    t.sidecars.PushBack(String(u8"SDL3.dll"));
    t.sidecars.PushBack(String(u8"dxcompiler.dll"));
    REQUIRE(ed::SaveTemplateManifest(*root.AsWritable(), t).IsOk());

    ed::ExportTemplate loaded;
    REQUIRE(ed::LoadTemplateManifest(root, loaded).IsOk());
    CHECK(loaded.id == u8"raptor-win64-0.1.0");
    CHECK(loaded.platform == u8"Win64");
    CHECK(loaded.playerBinary == u8"RaptorPlayer.exe");
    REQUIRE(loaded.sidecars.Size() == 2u);
    CHECK(loaded.sidecars[0] == u8"SDL3.dll");
    CHECK(loaded.sidecars[1] == u8"dxcompiler.dll");

    // Host synthesis: id/platform/player from the host; sidecars from "<player>.runtime-libs".
    SaveText(root, u8"RaptorPlayer.runtime-libs", u8"SDL3.dll\r\n\n  dxil.dll  \n");
    ed::ExportTemplate host;
    ed::SynthesizeHostTemplate(dir.AsView(), &root, host);
    CHECK(host.isHost);
    CHECK(host.platform == GetHostPlatformName());
    String expectedId(u8"host-"); expectedId += GetHostPlatformName();
    CHECK(host.id == expectedId.AsView());
    CHECK(host.playerBinary == GetExecutableName(u8"RaptorPlayer"));
    CHECK(host.directory == dir);
    REQUIRE(host.sidecars.Size() == 2u);          // blank line skipped, CR + spaces trimmed
    CHECK(host.sidecars[0] == u8"SDL3.dll");
    CHECK(host.sidecars[1] == u8"dxil.dll");

    NukeTree(dir.AsView());
}

TEST_CASE("export: template registry resolves by id, by platform, and host-falls-back")
{
    const String rootDir = TempDir(u8"draconic_templates_root");
    const String hostDir = TempDir(u8"draconic_host_tooldir");
    NukeTree(rootDir.AsView()); NukeTree(hostDir.AsView());
    REQUIRE(CreateDirectory(rootDir.AsView()));
    REQUIRE(CreateDirectory(hostDir.AsView()));
    REQUIRE(CreateDirectory(PathJoin(rootDir.AsView(), u8"win-template").AsView()));

    draconic::vfs::NativeFileSystem rootFs(rootDir.AsView());
    draconic::vfs::NativeFileSystem hostFs(hostDir.AsView());

    // One imported template for Win64 under <root>/win-template/template.xml.
    ed::ExportTemplate win;
    win.id = String(u8"raptor-win64-0.1.0"); win.platform = String(u8"Win64");
    win.playerBinary = String(u8"RaptorPlayer.exe");
    REQUIRE(ed::SaveTemplateManifest(*rootFs.AsWritable(), win, u8"win-template/template.xml").IsOk());

    ed::TemplateRegistry reg;
    reg.Refresh(rootDir.AsView(), &rootFs, hostDir.AsView(), &hostFs);
    CHECK(reg.Count() == 2u);   // imported win + synthesized host

    // Explicit id.
    const ed::ExportTemplate* byId = reg.FindById(u8"raptor-win64-0.1.0");
    REQUIRE(byId != nullptr);
    CHECK(byId->directory == PathJoin(rootDir.AsView(), u8"win-template"));

    // By platform: imported wins for Win64; host for the host platform.
    CHECK(reg.FindByPlatform(u8"Win64") == byId);
    const ed::ExportTemplate* hostT = reg.FindByPlatform(GetHostPlatformName());
    REQUIRE(hostT != nullptr);
    CHECK(hostT->isHost);

    // Resolve a preset: explicit id, blank-id-by-platform, and no-match => null.
    ed::ExportPreset p;
    p.platform = String(u8"Win64");
    CHECK(reg.Resolve(p) == byId);                 // blank templateId -> by platform
    p.templateId = String(u8"raptor-win64-0.1.0");
    CHECK(reg.Resolve(p) == byId);                 // explicit id
    ed::ExportPreset none; none.platform = String(u8"Nonexistent64");
    CHECK(reg.Resolve(none) == nullptr);

    NukeTree(rootDir.AsView()); NukeTree(hostDir.AsView());
}

TEST_CASE("export: ExportOne stages the resolved template's player + sidecars alongside content")
{
    const String projectDir = TempDir(u8"draconic_exportone_proj");
    const String toolDir = TempDir(u8"draconic_exportone_tool");
    const String outRoot = TempDir(u8"draconic_exportone_out");
    NukeTree(projectDir.AsView()); NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());

    // A minimal (asset-less) project - enough for the content pipeline; the driver test is about the
    // player/sidecar staging on top of it.
    REQUIRE(ed::EditorProject::Create(projectDir.AsView(), u8"ExportOneTest").IsOk());
    UniquePtr<ed::EditorProject> project = ed::EditorProject::Open(projectDir.AsView());
    REQUIRE(static_cast<bool>(project));
    REQUIRE(project->SaveSettings().IsOk());

    // Fake host tool dir: a "player" + its runtime-libs listing one sidecar + the sidecar file.
    REQUIRE(CreateDirectory(toolDir.AsView()));
    draconic::vfs::NativeFileSystem toolFs(toolDir.AsView());
    SaveText(toolFs, GetExecutableName(u8"RaptorPlayer").AsView(), u8"#!player\n");
    SaveText(toolFs, u8"RaptorPlayer.runtime-libs", u8"libfoo.so\n");
    SaveText(toolFs, u8"libfoo.so", u8"foo\n");

    ed::TemplateRegistry registry;
    registry.Refresh(StringView{}, nullptr, toolDir.AsView(), &toolFs);   // host template only

    ed::ExportPreset preset;
    preset.name = String(u8"Host Build");
    preset.platform = String(GetHostPlatformName());   // -> the host template
    preset.outputSubdir = String(u8"host");

    ed::BuilderRegistry builders;   // no assets -> no builders needed
    ed::ExportResult result;
    REQUIRE(ed::ExportOne(*project, preset, registry, builders, outRoot.AsView(), false, &result).IsOk());

    // The dist carries the player, the sidecar, and the content (Content.pak + player.xml).
    draconic::vfs::NativeFileSystem distFs(result.outputDir.AsView());
    CHECK(distFs.Exists(GetExecutableName(u8"RaptorPlayer").AsView()));
    CHECK(distFs.Exists(u8"libfoo.so"));
    CHECK(distFs.Exists(u8"Content.pak"));
    CHECK(distFs.Exists(u8"player.xml"));
    CHECK(result.filesStaged == 2u);   // player + one sidecar (no additionalFiles)

    NukeTree(projectDir.AsView()); NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());
}
