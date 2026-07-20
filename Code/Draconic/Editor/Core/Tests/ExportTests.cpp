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
import draconic.settings;

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
        // Pre-transcode the scene stream like the editor/CLI do: the staged pak carries
        // the BINARY wire even though the source (SaveScene) is XML now.
        HashMap<Guid, Array<byte>> sceneStreams;
        {
            UniquePtr<IStream> src = sceneInstance->ReadData(u8"scene");
            REQUIRE(src.Get() != nullptr);
            dscene::Scene scratch(u8"scratch");
            scratch.AddSystem<draconic::render::MeshComponentManager>();
            Result<Array<byte>> bytes =
                dscene::TranscodeSceneStreamToBinary(*src, scratch, /*includeSettings=*/true);
            REQUIRE(bytes.HasValue());
            REQUIRE(bytes.Value().Size() > 0);
            CHECK(bytes.Value()[0] != static_cast<byte>(u8'<'));   // binary, not the XML source
            sceneStreams.InsertOrAssign(sceneId, Move(bytes.Value()));
        }
        ed::ExportStats stats;
        REQUIRE(ed::ExportProject(*project, distDir, registry, false, &stats, {}, &sceneStreams).IsOk());
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

    // The PACKED stream is the binary wire (the transcode map was honored).
    {
        UniquePtr<IStream> packed = sceneInstance->ReadData(u8"scene");
        REQUIRE(packed.Get() != nullptr);
        byte first = static_cast<byte>(0);
        REQUIRE(packed->Read(&first, 1) == 1u);
        CHECK(first != static_cast<byte>(u8'<'));
    }
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
    b.config = String(u8"RelWithDebInfo"); b.stageSymbols = true;
    b.pruneToReachable = true;
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

    CHECK_FALSE(loaded.presets[0].pruneToReachable);   // default (unset) stays false

    const ed::ExportPreset* win = loaded.Find(u8"Windows Desktop");
    REQUIRE(win != nullptr);
    CHECK(win->platform == u8"Win64");
    CHECK(win->config == u8"RelWithDebInfo");   // config axis round-trips
    CHECK(win->stageSymbols);                   // symbols opt-in round-trips
    CHECK(win->pruneToReachable);               // pruning opt-in round-trips
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

    // A scene/prefab reference scanner for the pruning tests (mirrors the CLI's MakeSceneScanner but
    // with just the manager these tests use). Loads the instance, resolves its Refs through a
    // factory-less ResourceManager so every bound id lands in CollectUnresolved, and reads back the
    // parked prefab instances (the scene->prefab->asset chain).
    ed::SceneReferenceScanner MakePruningScanner()
    {
        return [](draconic::content::Instance& instance, draconic::content::ContentDatabase& db,
                  ed::SceneReferences& out)
        {
            dscene::Scene scene;
            scene.AddSystem<draconic::render::MeshComponentManager>();
            if (!dscene::LoadScene(instance, scene).IsOk()) { return; }
            draconic::resource::ResourceManager collector(db);
            dscene::ResolveSceneResources(scene, collector);
            collector.CollectUnresolved(out.resources);
            scene.ForEachPendingPrefabInstance([&out](dscene::Scene::PendingPrefabInstance& pending)
            {
                out.prefabs.PushBack(pending.prefabId);
            });
        };
    }

    // Author a cube StaticMeshAsset under `group` and return its guid.
    Guid AuthorMesh(draconic::content::Group& group, StringView name)
    {
        draconic::content::Instance* inst = group.CreateInstance(name, geo::StaticMeshAsset::StaticType());
        REQUIRE(inst != nullptr);
        geo::StaticMeshAsset asset;
        geo::MeshImporter::Import(*geo::Primitives::Cube(2.0f), asset);
        REQUIRE(inst->WriteObject(asset).IsOk());
        return inst->Id();
    }

    // A minimal host export template in `toolDir` (fake player + no sidecars). Returns a preset
    // targeting it (blank templateId => resolve by host platform).
    void SetupHostTemplate(StringView toolDir, ed::TemplateRegistry& registry)
    {
        REQUIRE(CreateDirectory(toolDir));
        draconic::vfs::NativeFileSystem toolFs(toolDir);
        SaveText(toolFs, GetExecutableName(u8"RaptorPlayer").AsView(), u8"#!player\n");
        registry.Refresh(StringView{}, nullptr, toolDir, &toolFs);
    }
}

TEST_CASE("export: template.xml round-trips + host synthesis reads its runtime-libs")
{
    const String dir = TempDir(u8"draconic_template_test");
    NukeTree(dir.AsView());
    REQUIRE(CreateDirectory(dir.AsView()));
    draconic::vfs::NativeFileSystem root(dir.AsView());

    // template.xml round-trip, including the v2 (platform, config) axis: config + compiler + symbols[].
    ed::ExportTemplate t;
    t.id = String(u8"raptor-win64-release-0.1.0"); t.name = String(u8"Windows Desktop Release 0.1.0");
    t.platform = String(u8"Win64"); t.config = String(u8"Release"); t.compiler = String(u8"MSVC");
    t.engineVersion = String(u8"0.1.0");
    t.playerBinary = String(u8"RaptorPlayer.exe");
    t.sidecars.PushBack(String(u8"SDL3.dll"));
    t.sidecars.PushBack(String(u8"dxcompiler.dll"));
    t.symbols.PushBack(String(u8"RaptorPlayer.pdb"));
    REQUIRE(ed::SaveTemplateManifest(*root.AsWritable(), t).IsOk());

    ed::ExportTemplate loaded;
    REQUIRE(ed::LoadTemplateManifest(root, loaded).IsOk());
    CHECK(loaded.id == u8"raptor-win64-release-0.1.0");
    CHECK(loaded.platform == u8"Win64");
    CHECK(loaded.config == u8"Release");
    CHECK(loaded.compiler == u8"MSVC");
    CHECK(loaded.playerBinary == u8"RaptorPlayer.exe");
    REQUIRE(loaded.sidecars.Size() == 2u);
    CHECK(loaded.sidecars[0] == u8"SDL3.dll");
    CHECK(loaded.sidecars[1] == u8"dxcompiler.dll");
    REQUIRE(loaded.symbols.Size() == 1u);
    CHECK(loaded.symbols[0] == u8"RaptorPlayer.pdb");

    // Host synthesis: id/platform/config/player from the host; sidecars from "<player>.runtime-libs".
    SaveText(root, u8"RaptorPlayer.runtime-libs", u8"SDL3.dll\r\n\n  dxil.dll  \n");
    ed::ExportTemplate host;
    ed::SynthesizeHostTemplate(dir.AsView(), &root, host);
    CHECK(host.isHost);
    CHECK(host.platform == GetHostPlatformName());
    CHECK(host.config == GetBuildConfigName());   // the config that built this test binary
    // Host id carries the config: "host-<platform>-<config>".
    String expectedId(u8"host-"); expectedId += GetHostPlatformName();
    expectedId += u8"-"; expectedId += GetBuildConfigName();
    CHECK(host.id == expectedId.AsView());
    CHECK(host.playerBinary == GetExecutableName(u8"RaptorPlayer"));
    CHECK(host.directory == dir);
    REQUIRE(host.sidecars.Size() == 2u);          // blank line skipped, CR + spaces trimmed
    CHECK(host.sidecars[0] == u8"SDL3.dll");
    CHECK(host.sidecars[1] == u8"dxil.dll");

    NukeTree(dir.AsView());
}

TEST_CASE("export: a v1 template.xml without a config field reads as Release (back-compat)")
{
    const String dir = TempDir(u8"draconic_template_v1_backcompat");
    NukeTree(dir.AsView());
    REQUIRE(CreateDirectory(dir.AsView()));
    draconic::vfs::NativeFileSystem root(dir.AsView());

    // A hand-written v1 manifest: the OLD schema (dataVersion 1, no config/compiler/symbols). This is
    // exactly what a pre-config-axis editor wrote; it must still load, defaulting config -> Release.
    const StringView v1 =
        u8"<root>"
        u8"<array name=\"dataVersions\" count=\"1\">"
        u8"<u64 name=\"type\">0</u64><u32 name=\"version\">1</u32>"
        u8"</array>"
        u8"<string name=\"id\">raptor-legacy</string>"
        u8"<string name=\"name\">Legacy</string>"
        u8"<string name=\"platform\">Win64</string>"
        u8"<string name=\"engineVersion\">0.1.0</string>"
        u8"<string name=\"playerBinary\">RaptorPlayer.exe</string>"
        u8"<array name=\"sidecars\" count=\"1\"><string>SDL3.dll</string></array>"
        u8"<string name=\"notes\"></string>"
        u8"</root>";
    SaveText(root, u8"template.xml", v1);

    ed::ExportTemplate loaded;
    REQUIRE(ed::LoadTemplateManifest(root, loaded).IsOk());
    CHECK(loaded.id == u8"raptor-legacy");
    CHECK(loaded.platform == u8"Win64");
    CHECK(loaded.config == u8"Release");           // absent config normalizes to Release
    CHECK(loaded.EffectiveConfig() == u8"Release");
    CHECK(loaded.compiler.IsEmpty());
    REQUIRE(loaded.sidecars.Size() == 1u);         // the old required sidecars still map through
    CHECK(loaded.sidecars[0] == u8"SDL3.dll");
    CHECK(loaded.symbols.IsEmpty());

    NukeTree(dir.AsView());
}

TEST_CASE("export: template registry resolves by id, by platform, and host-falls-back")
{
    const String rootDir = TempDir(u8"draconic_templates_root");
    const String hostDir = TempDir(u8"draconic_host_tooldir");
    NukeTree(rootDir.AsView()); NukeTree(hostDir.AsView());
    REQUIRE(CreateDirectory(rootDir.AsView()));
    REQUIRE(CreateDirectory(hostDir.AsView()));
    REQUIRE(CreateDirectory(PathJoin(rootDir.AsView(), u8"foreign-template").AsView()));

    draconic::vfs::NativeFileSystem rootFs(rootDir.AsView());
    draconic::vfs::NativeFileSystem hostFs(hostDir.AsView());

    // One imported template for a NON-host platform, so the host-fallback check below is valid on
    // every host: if the import shared the host platform it would out-rank the synthesized host
    // (that branch has its own test). Pick whichever of Win64/Linux64 is not the current host.
    const StringView hostPlatform = GetHostPlatformName();
    const String foreignPlatform = (hostPlatform == StringView(u8"Win64"))
        ? String(u8"Linux64") : String(u8"Win64");

    ed::ExportTemplate foreign;
    foreign.id = String(u8"raptor-foreign-0.1.0"); foreign.platform = foreignPlatform;
    foreign.playerBinary = String(u8"RaptorPlayer");
    REQUIRE(ed::SaveTemplateManifest(*rootFs.AsWritable(), foreign, u8"foreign-template/template.xml").IsOk());

    ed::TemplateRegistry reg;
    reg.Refresh(rootDir.AsView(), &rootFs, hostDir.AsView(), &hostFs);
    CHECK(reg.Count() == 2u);   // imported foreign + synthesized host

    // Explicit id.
    const ed::ExportTemplate* byId = reg.FindById(u8"raptor-foreign-0.1.0");
    REQUIRE(byId != nullptr);
    CHECK(byId->directory == PathJoin(rootDir.AsView(), u8"foreign-template"));

    // By (platform, config): the imported (Release) template answers its own platform; the host
    // platform falls back to the synthesized host template (whatever config built this binary).
    CHECK(reg.FindBy(foreignPlatform.AsView(), u8"Release") == byId);
    CHECK(reg.FindBy(foreignPlatform.AsView(), u8"") == byId);   // empty config => Release
    const ed::ExportTemplate* hostT = reg.FindBy(hostPlatform, GetBuildConfigName());
    REQUIRE(hostT != nullptr);
    CHECK(hostT->isHost);

    // Resolve a preset: blank-id-by-(platform,config), explicit id, and no-match => null.
    ed::ExportPreset p;
    p.platform = foreignPlatform;   // blank config -> Release, which the imported foreign template is
    CHECK(reg.Resolve(p) == byId);                 // blank templateId -> by (platform, config)
    p.templateId = String(u8"raptor-foreign-0.1.0");
    CHECK(reg.Resolve(p) == byId);                 // explicit id
    ed::ExportPreset none; none.platform = String(u8"Nonexistent64");
    CHECK(reg.Resolve(none) == nullptr);

    NukeTree(rootDir.AsView()); NukeTree(hostDir.AsView());
}

TEST_CASE("export: an imported template out-ranks the synthesized host for the host platform")
{
    const String rootDir = TempDir(u8"draconic_templates_hostwin");
    const String hostDir = TempDir(u8"draconic_host_tooldir2");
    NukeTree(rootDir.AsView()); NukeTree(hostDir.AsView());
    REQUIRE(CreateDirectory(rootDir.AsView()));
    REQUIRE(CreateDirectory(hostDir.AsView()));
    REQUIRE(CreateDirectory(PathJoin(rootDir.AsView(), u8"host-template").AsView()));

    draconic::vfs::NativeFileSystem rootFs(rootDir.AsView());
    draconic::vfs::NativeFileSystem hostFs(hostDir.AsView());

    // An imported template for the SAME platform AND config as this host build - so the exact-match
    // pass returns both, and the imported (non-host) bundle must win the tiebreak.
    ed::ExportTemplate imported;
    imported.id = String(u8"raptor-host-import"); imported.platform = String(GetHostPlatformName());
    imported.config = String(GetBuildConfigName());
    imported.playerBinary = String(u8"RaptorPlayer");
    REQUIRE(ed::SaveTemplateManifest(*rootFs.AsWritable(), imported, u8"host-template/template.xml").IsOk());

    ed::TemplateRegistry reg;
    reg.Refresh(rootDir.AsView(), &rootFs, hostDir.AsView(), &hostFs);
    CHECK(reg.Count() == 2u);   // imported + synthesized host (same platform)

    // FindBy prefers the real imported bundle over the synthesized host template (platform-only
    // fallback: the imported template has no config => Release, and outranks the host).
    const ed::ExportTemplate* byPlatform = reg.FindBy(GetHostPlatformName(), GetBuildConfigName());
    REQUIRE(byPlatform != nullptr);
    CHECK_FALSE(byPlatform->isHost);
    CHECK(byPlatform->id == u8"raptor-host-import");

    // The synthesized host template is still present, reachable by its "host-<platform>-<config>" id.
    String hostId(u8"host-"); hostId += GetHostPlatformName();
    hostId += u8"-"; hostId += GetBuildConfigName();
    const ed::ExportTemplate* host = reg.FindById(hostId.AsView());
    REQUIRE(host != nullptr);
    CHECK(host->isHost);

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
    // The host template is stamped with this build's engine version, so no soft-mismatch warning.
    CHECK(result.engineVersionWarning.IsEmpty());

    NukeTree(projectDir.AsView()); NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());
}

TEST_CASE("export: a template built against a different engine version warns but still exports")
{
    const String projectDir = TempDir(u8"draconic_ev_proj");
    const String rootDir = TempDir(u8"draconic_ev_root");
    const String toolDir = TempDir(u8"draconic_ev_tool");
    const String outRoot = TempDir(u8"draconic_ev_out");
    NukeTree(projectDir.AsView()); NukeTree(rootDir.AsView());
    NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());

    REQUIRE(ed::EditorProject::Create(projectDir.AsView(), u8"EngineVersionTest").IsOk());
    UniquePtr<ed::EditorProject> project = ed::EditorProject::Open(projectDir.AsView());
    REQUIRE(static_cast<bool>(project));
    REQUIRE(project->SaveSettings().IsOk());

    // An imported template for the host platform stamped with a DIFFERENT engine version.
    REQUIRE(CreateDirectory(rootDir.AsView()));
    REQUIRE(CreateDirectory(PathJoin(rootDir.AsView(), u8"old-template").AsView()));
    draconic::vfs::NativeFileSystem rootFs(rootDir.AsView());
    ed::ExportTemplate old;
    old.id = String(u8"raptor-old-engine"); old.platform = String(GetHostPlatformName());
    old.engineVersion = String(u8"0.0.0-ancient");
    old.playerBinary = GetExecutableName(u8"RaptorPlayer");
    REQUIRE(ed::SaveTemplateManifest(*rootFs.AsWritable(), old, u8"old-template/template.xml").IsOk());
    // The player file the driver stages from the template dir.
    draconic::vfs::NativeFileSystem oldDirFs(PathJoin(rootDir.AsView(), u8"old-template").AsView());
    SaveText(oldDirFs, GetExecutableName(u8"RaptorPlayer").AsView(), u8"#!player\n");

    REQUIRE(CreateDirectory(toolDir.AsView()));
    draconic::vfs::NativeFileSystem toolFs(toolDir.AsView());
    ed::TemplateRegistry registry;
    registry.Refresh(rootDir.AsView(), &rootFs, toolDir.AsView(), &toolFs);

    ed::ExportPreset preset;
    preset.name = String(u8"Old Engine Build");
    preset.templateId = String(u8"raptor-old-engine");   // resolve to the mismatched template
    preset.outputSubdir = String(u8"old");

    ed::BuilderRegistry builders;
    ed::ExportResult result;
    // Export still SUCCEEDS (soft match) ...
    REQUIRE(ed::ExportOne(*project, preset, registry, builders, outRoot.AsView(), false, &result).IsOk());
    // ... but records the version mismatch for the caller to surface.
    CHECK_FALSE(result.engineVersionWarning.IsEmpty());

    NukeTree(projectDir.AsView()); NukeTree(rootDir.AsView());
    NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());
}

TEST_CASE("export: ImportTemplate installs a bundle the registry then resolves")
{
    const String src = TempDir(u8"draconic_tmpl_src");
    const String root = TempDir(u8"draconic_tmpl_root2");
    NukeTree(src.AsView()); NukeTree(root.AsView());
    REQUIRE(CreateDirectory(src.AsView()));

    // A source bundle: template.xml + a fake player + a sidecar.
    draconic::vfs::NativeFileSystem srcFs(src.AsView());
    ed::ExportTemplate t;
    t.id = String(u8"raptor-win64-import"); t.platform = String(u8"Win64");
    t.playerBinary = String(u8"RaptorPlayer.exe"); t.sidecars.PushBack(String(u8"SDL3.dll"));
    REQUIRE(ed::SaveTemplateManifest(*srcFs.AsWritable(), t).IsOk());
    SaveText(srcFs, u8"RaptorPlayer.exe", u8"exe\n");
    SaveText(srcFs, u8"SDL3.dll", u8"dll\n");

    String importedId;
    REQUIRE(ed::ImportTemplate(src.AsView(), root.AsView(), &importedId).IsOk());
    CHECK(importedId == u8"raptor-win64-import");

    // The registry over the root now resolves it (alongside the synthesized host template).
    draconic::vfs::NativeFileSystem rootFs(root.AsView());
    draconic::vfs::NativeFileSystem toolFs(src.AsView());   // any dir for the host template
    ed::TemplateRegistry reg;
    reg.Refresh(root.AsView(), &rootFs, src.AsView(), &toolFs);
    const ed::ExportTemplate* found = reg.FindById(u8"raptor-win64-import");
    REQUIRE(found != nullptr);
    CHECK(found->platform == u8"Win64");
    REQUIRE(found->sidecars.Size() == 1u);
    CHECK(found->sidecars[0] == u8"SDL3.dll");
    CHECK(found->directory == PathJoin(root.AsView(), u8"raptor-win64-import"));

    // A source with no template.xml fails.
    const String empty = TempDir(u8"draconic_tmpl_empty");
    NukeTree(empty.AsView()); REQUIRE(CreateDirectory(empty.AsView()));
    CHECK_FALSE(ed::ImportTemplate(empty.AsView(), root.AsView()).IsOk());

    NukeTree(src.AsView()); NukeTree(root.AsView()); NukeTree(empty.AsView());
}

TEST_CASE("export: CreateTemplate packages a Bin/<Config> dir and the registry then resolves it")
{
    const String base = TempDir(u8"draconic_createtmpl");
    const String root = TempDir(u8"draconic_createtmpl_root");
    NukeTree(base.AsView()); NukeTree(root.AsView());

    // A fake build dir with the canonical layout "…/Bin/<Config>/<Platform>-<Compiler>", holding a
    // player + its runtime-libs manifest + the one sidecar it lists.
    String leaf(GetHostPlatformName()); leaf += u8"-Clang";
    const String binDir = PathJoin(PathJoin(PathJoin(base.AsView(), u8"Bin").AsView(), u8"Release").AsView(),
                                   leaf.AsView());
    REQUIRE(CreateDirectories(binDir.AsView()));
    draconic::vfs::NativeFileSystem binFs(binDir.AsView());
    SaveText(binFs, GetExecutableName(u8"RaptorPlayer").AsView(), u8"#!player\n");
    SaveText(binFs, u8"RaptorPlayer.runtime-libs", u8"libfoo.so\n");
    SaveText(binFs, u8"libfoo.so", u8"foo\n");

    // Install mode: writes into <root>/<id>. config/compiler come from the packaged dir path.
    String createdId, createdDir;
    REQUIRE(ed::CreateTemplate(binDir.AsView(), root.AsView(), ed::TemplateOutput::Install,
                               &createdId, &createdDir).IsOk());
    // id = raptor-<platform>-<config>-<engineVersion> (lowercased platform/config).
    String expectedId(u8"raptor-");
    expectedId += ed::AsciiLower(GetHostPlatformName());
    expectedId += u8"-release-";
    expectedId += draconic::project::kEngineVersionString;
    CHECK(createdId == expectedId.AsView());
    CHECK(createdDir == PathJoin(root.AsView(), createdId.AsView()));

    // The bundle exists on disk: manifest + player + the sidecar.
    draconic::vfs::NativeFileSystem bundleFs(createdDir.AsView());
    CHECK(bundleFs.Exists(u8"template.xml"));
    CHECK(bundleFs.Exists(GetExecutableName(u8"RaptorPlayer").AsView()));
    CHECK(bundleFs.Exists(u8"libfoo.so"));

    // The manifest stamped config = Release (from the dir), compiler = Clang (from the leaf).
    ed::ExportTemplate manifest;
    REQUIRE(ed::LoadTemplateManifest(bundleFs, manifest).IsOk());
    CHECK(manifest.config == u8"Release");
    CHECK(manifest.compiler == u8"Clang");
    CHECK(manifest.platform == GetHostPlatformName());
    REQUIRE(manifest.sidecars.Size() == 1u);
    CHECK(manifest.sidecars[0] == u8"libfoo.so");

    // The registry over the root now finds the created template.
    draconic::vfs::NativeFileSystem rootFs(root.AsView());
    draconic::vfs::NativeFileSystem toolFs(base.AsView());   // any dir for the host template
    ed::TemplateRegistry reg;
    reg.Refresh(root.AsView(), &rootFs, base.AsView(), &toolFs);
    const ed::ExportTemplate* found = reg.FindById(createdId.AsView());
    REQUIRE(found != nullptr);
    CHECK_FALSE(found->isHost);
    CHECK(found->config == u8"Release");
    CHECK(reg.FindBy(GetHostPlatformName(), u8"Release") == found);   // resolves by (platform, config)

    NukeTree(base.AsView()); NukeTree(root.AsView());
}

TEST_CASE("export: CreateTemplate --out mode writes a self-contained bundle to the folder")
{
    const String base = TempDir(u8"draconic_createtmpl_out");
    const String outFolder = TempDir(u8"draconic_createtmpl_bundle");
    NukeTree(base.AsView()); NukeTree(outFolder.AsView());

    String leaf(GetHostPlatformName()); leaf += u8"-GCC";
    const String binDir = PathJoin(PathJoin(PathJoin(base.AsView(), u8"Bin").AsView(), u8"Debug").AsView(),
                                   leaf.AsView());
    REQUIRE(CreateDirectories(binDir.AsView()));
    draconic::vfs::NativeFileSystem binFs(binDir.AsView());
    SaveText(binFs, GetExecutableName(u8"RaptorPlayer").AsView(), u8"#!player\n");
    // No runtime-libs manifest => no sidecars (an rpath-style build); the player alone still packages.

    String createdId, createdDir;
    REQUIRE(ed::CreateTemplate(binDir.AsView(), outFolder.AsView(), ed::TemplateOutput::ExportFolder,
                               &createdId, &createdDir).IsOk());
    // ExportFolder writes straight into the given folder (zip it to distribute).
    CHECK(createdDir == outFolder);
    draconic::vfs::NativeFileSystem bundleFs(outFolder.AsView());
    CHECK(bundleFs.Exists(u8"template.xml"));
    CHECK(bundleFs.Exists(GetExecutableName(u8"RaptorPlayer").AsView()));

    ed::ExportTemplate manifest;
    REQUIRE(ed::LoadTemplateManifest(bundleFs, manifest).IsOk());
    CHECK(manifest.config == u8"Debug");   // from the Bin/Debug path
    CHECK(manifest.compiler == u8"GCC");

    // A missing player binary is a hard failure (nothing to package).
    const String emptyBin = PathJoin(base.AsView(), u8"empty");
    REQUIRE(CreateDirectories(emptyBin.AsView()));
    CHECK_FALSE(ed::CreateTemplate(emptyBin.AsView(), outFolder.AsView(),
                                   ed::TemplateOutput::ExportFolder).IsOk());

    NukeTree(base.AsView()); NukeTree(outFolder.AsView());
}

TEST_CASE("export: FindBy resolves exact (platform,config) and falls back preferring Release")
{
    const String rootDir = TempDir(u8"draconic_findby_root");
    const String hostDir = TempDir(u8"draconic_findby_host");
    NukeTree(rootDir.AsView()); NukeTree(hostDir.AsView());
    REQUIRE(CreateDirectory(rootDir.AsView()));
    REQUIRE(CreateDirectory(hostDir.AsView()));

    // Two imported templates for the SAME non-host platform: a Debug one and a Release one.
    const StringView hostPlatform = GetHostPlatformName();
    const String plat = (hostPlatform == StringView(u8"Win64")) ? String(u8"Linux64") : String(u8"Win64");

    const auto writeTemplate = [&](StringView id, StringView cfg, StringView subdir)
    {
        REQUIRE(CreateDirectory(PathJoin(rootDir.AsView(), subdir).AsView()));
        draconic::vfs::NativeFileSystem rootFs(rootDir.AsView());
        ed::ExportTemplate t;
        t.id = String(id); t.platform = plat; t.config = String(cfg);
        t.playerBinary = String(u8"RaptorPlayer");
        const String manifestPath = PathJoin(subdir, u8"template.xml");
        REQUIRE(ed::SaveTemplateManifest(*rootFs.AsWritable(), t, manifestPath.AsView()).IsOk());
    };
    writeTemplate(u8"raptor-dbg", u8"Debug", u8"dbg");
    writeTemplate(u8"raptor-rel", u8"Release", u8"rel");

    draconic::vfs::NativeFileSystem rootFs(rootDir.AsView());
    draconic::vfs::NativeFileSystem hostFs(hostDir.AsView());
    ed::TemplateRegistry reg;
    reg.Refresh(rootDir.AsView(), &rootFs, hostDir.AsView(), &hostFs);

    // Exact match by config.
    const ed::ExportTemplate* dbg = reg.FindById(u8"raptor-dbg");
    const ed::ExportTemplate* rel = reg.FindById(u8"raptor-rel");
    REQUIRE(dbg != nullptr); REQUIRE(rel != nullptr);
    CHECK(reg.FindBy(plat.AsView(), u8"Debug") == dbg);
    CHECK(reg.FindBy(plat.AsView(), u8"Release") == rel);

    // No RelWithDebInfo template for this platform => platform-only fallback prefers the Release one.
    CHECK(reg.FindBy(plat.AsView(), u8"RelWithDebInfo") == rel);
    // Empty config defaults to Release (exact match).
    CHECK(reg.FindBy(plat.AsView(), u8"") == rel);

    // A preset selecting (platform, Debug) resolves to the Debug template.
    ed::ExportPreset p; p.platform = plat; p.config = String(u8"Debug");
    CHECK(reg.Resolve(p) == dbg);
    p.config = String(u8"Release");
    CHECK(reg.Resolve(p) == rel);
    p.config.Clear();                      // blank => Release
    CHECK(reg.Resolve(p) == rel);

    NukeTree(rootDir.AsView()); NukeTree(hostDir.AsView());
}

TEST_CASE("export: ExportOne stages template symbols only when the preset opts in")
{
    const String projectDir = TempDir(u8"draconic_sym_proj");
    const String rootDir = TempDir(u8"draconic_sym_root");
    const String toolDir = TempDir(u8"draconic_sym_tool");
    const String outRoot = TempDir(u8"draconic_sym_out");
    NukeTree(projectDir.AsView()); NukeTree(rootDir.AsView());
    NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());

    REQUIRE(ed::EditorProject::Create(projectDir.AsView(), u8"SymbolsTest").IsOk());
    UniquePtr<ed::EditorProject> project = ed::EditorProject::Open(projectDir.AsView());
    REQUIRE(static_cast<bool>(project));
    REQUIRE(project->SaveSettings().IsOk());

    // An imported template with a player, one required sidecar, and one SYMBOL file.
    REQUIRE(CreateDirectory(rootDir.AsView()));
    REQUIRE(CreateDirectory(PathJoin(rootDir.AsView(), u8"sym-template").AsView()));
    draconic::vfs::NativeFileSystem rootFs(rootDir.AsView());
    ed::ExportTemplate t;
    t.id = String(u8"raptor-sym"); t.platform = String(GetHostPlatformName());
    t.playerBinary = GetExecutableName(u8"RaptorPlayer");
    t.sidecars.PushBack(String(u8"libfoo.so"));
    t.symbols.PushBack(String(u8"RaptorPlayer.debug"));
    REQUIRE(ed::SaveTemplateManifest(*rootFs.AsWritable(), t, u8"sym-template/template.xml").IsOk());
    draconic::vfs::NativeFileSystem tmplDirFs(PathJoin(rootDir.AsView(), u8"sym-template").AsView());
    SaveText(tmplDirFs, GetExecutableName(u8"RaptorPlayer").AsView(), u8"#!player\n");
    SaveText(tmplDirFs, u8"libfoo.so", u8"foo\n");
    SaveText(tmplDirFs, u8"RaptorPlayer.debug", u8"dwarf\n");

    REQUIRE(CreateDirectory(toolDir.AsView()));
    draconic::vfs::NativeFileSystem toolFs(toolDir.AsView());
    ed::TemplateRegistry registry;
    registry.Refresh(rootDir.AsView(), &rootFs, toolDir.AsView(), &toolFs);

    ed::BuilderRegistry builders;

    // Default preset: symbols stripped from the dist (sidecar staged, symbol not).
    {
        ed::ExportPreset preset;
        preset.name = String(u8"Stripped"); preset.templateId = String(u8"raptor-sym");
        preset.outputSubdir = String(u8"stripped");
        ed::ExportResult result;
        REQUIRE(ed::ExportOne(*project, preset, registry, builders, outRoot.AsView(), false, &result).IsOk());
        draconic::vfs::NativeFileSystem distFs(result.outputDir.AsView());
        CHECK(distFs.Exists(GetExecutableName(u8"RaptorPlayer").AsView()));
        CHECK(distFs.Exists(u8"libfoo.so"));                 // required sidecar always staged
        CHECK_FALSE(distFs.Exists(u8"RaptorPlayer.debug"));  // symbols stripped by default
        CHECK(result.filesStaged == 2u);                     // player + sidecar
    }

    // Opt-in preset: symbols staged too.
    {
        ed::ExportPreset preset;
        preset.name = String(u8"WithSymbols"); preset.templateId = String(u8"raptor-sym");
        preset.outputSubdir = String(u8"symbols"); preset.stageSymbols = true;
        ed::ExportResult result;
        REQUIRE(ed::ExportOne(*project, preset, registry, builders, outRoot.AsView(), false, &result).IsOk());
        draconic::vfs::NativeFileSystem distFs(result.outputDir.AsView());
        CHECK(distFs.Exists(u8"RaptorPlayer.debug"));        // opted in
        CHECK(result.filesStaged == 3u);                     // player + sidecar + symbol
    }

    NukeTree(projectDir.AsView()); NukeTree(rootDir.AsView());
    NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());
}

TEST_CASE("export: a v1 export_presets.xml without config/stageSymbols reads as Release/false")
{
    const String dir = TempDir(u8"draconic_presets_v1");
    NukeTree(dir.AsView());
    REQUIRE(CreateDirectory(dir.AsView()));
    draconic::vfs::NativeFileSystem root(dir.AsView());

    // A hand-written v1 export_presets.xml (dataVersion 1, no config/stageSymbols on the preset).
    const StringView v1 =
        u8"<root>"
        u8"<array name=\"dataVersions\" count=\"1\">"
        u8"<u64 name=\"type\">0</u64><u32 name=\"version\">1</u32>"
        u8"</array>"
        u8"<array name=\"presets\" count=\"1\">"
        u8"<object>"
        u8"<string name=\"name\">Legacy</string>"
        u8"<string name=\"platform\">Win64</string>"
        u8"<string name=\"templateId\"></string>"
        u8"<string name=\"playerName\"></string>"
        u8"<string name=\"outputSubdir\">Win64</string>"
        u8"<array name=\"additionalFiles\" count=\"0\"></array>"
        u8"</object>"
        u8"</array>"
        u8"</root>";
    SaveText(root, u8"export_presets.xml", v1);

    ed::ExportPresetSet loaded;
    REQUIRE(ed::LoadExportPresets(root, loaded).IsOk());
    REQUIRE(loaded.presets.Size() == 1u);
    CHECK(loaded.presets[0].name == u8"Legacy");
    CHECK(loaded.presets[0].config.IsEmpty());          // absent => resolves as Release
    CHECK_FALSE(loaded.presets[0].stageSymbols);        // absent => stripped
    CHECK_FALSE(loaded.presets[0].pruneToReachable);    // absent => pack everything (back-compat)

    NukeTree(dir.AsView());
}

TEST_CASE("export: ResolveTemplatesRoot prefers an explicit override")
{
    // An explicit override (the editor's EditorExportSettings::templatesRoot) wins verbatim.
    CHECK(ed::ResolveTemplatesRoot(u8"/shared/templates") == u8"/shared/templates");
    // An empty override falls through to env/default - a non-empty root, same as the no-arg form
    // the CLI uses (the two surfaces resolve identically when no setting is present).
    const String fallback = ed::ResolveTemplatesRoot(u8"");
    CHECK_FALSE(fallback.IsEmpty());
    CHECK(fallback == ed::ResolveTemplatesRoot());
}

TEST_CASE("export: EditorExportSettings round-trips through the editor settings store")
{
    namespace settings = draconic::settings;
    ed::RegisterEditorSettingsTypes();   // so Settings::Load can instantiate the section

    const String dir = TempDir(u8"draconic_editor_settings");
    NukeTree(dir.AsView());
    REQUIRE(CreateDirectory(dir.AsView()));
    draconic::vfs::NativeFileSystem root(dir.AsView());

    // First run: no file => NotFound, and the section is absent (reads as its defaults on access).
    {
        settings::Settings store;
        CHECK_FALSE(ed::LoadEditorSettings(root, store).IsOk());
        CHECK(store.Find<ed::EditorExportSettings>() == nullptr);
    }

    // Author a store with a custom templates root and persist it.
    {
        settings::Settings store;
        store.Section<ed::EditorExportSettings>().templatesRoot = String(u8"/shared/templates");
        REQUIRE(ed::SaveEditorSettings(*root.AsWritable(), store).IsOk());
    }

    // Load it back into a fresh store - the override survives the XML round-trip.
    {
        settings::Settings store;
        REQUIRE(ed::LoadEditorSettings(root, store).IsOk());
        const ed::EditorExportSettings* s = store.Find<ed::EditorExportSettings>();
        REQUIRE(s != nullptr);
        CHECK(s->templatesRoot == u8"/shared/templates");
    }

    NukeTree(dir.AsView());
}

TEST_CASE("export: pruned dist keeps the referenced closure, drops the rest, and reports both")
{
    GlobalTypeRegistry().Register(dscene::SceneDocument::StaticType());
    RegisterSerializable<dscene::SceneDocument>();
    geo::RegisterMeshAssets();
    GlobalTypeRegistry().Register(geo::StaticMeshSource::StaticType());
    RegisterSerializable<geo::StaticMeshSource>();

    const String projectDir = TempDir(u8"draconic_prune_proj");
    const String toolDir    = TempDir(u8"draconic_prune_tool");
    const String outRoot    = TempDir(u8"draconic_prune_out");
    NukeTree(projectDir.AsView()); NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());

    REQUIRE(ed::EditorProject::Create(projectDir.AsView(), u8"Prune").IsOk());
    UniquePtr<ed::EditorProject> project = ed::EditorProject::Open(projectDir.AsView());
    REQUIRE(static_cast<bool>(project));

    // Two authored meshes; the scene references only the first.
    draconic::content::Group* meshes = project->SourceDb().RootGroup()->CreateGroup(u8"Meshes");
    const Guid meshRefId  = AuthorMesh(*meshes, u8"Referenced");
    const Guid meshDeadId = AuthorMesh(*meshes, u8"Unreferenced");

    draconic::content::Group* scenes = project->SourceDb().RootGroup()->CreateGroup(u8"Scenes");
    draconic::content::Instance* sceneInst =
        scenes->CreateInstance(u8"Main", dscene::SceneDocument::StaticType());
    REQUIRE(sceneInst != nullptr);
    { dscene::SceneDocument doc; doc.name = String(u8"Main"); REQUIRE(sceneInst->WriteObject(doc).IsOk()); }
    const Guid sceneId = sceneInst->Id();
    {
        dscene::Scene scene(u8"Main");
        scene.AddSystem<draconic::render::MeshComponentManager>();
        const dscene::EntityHandle e = scene.CreateEntity(u8"Box");
        scene.GetSystem<draconic::render::MeshComponentManager>()->Add(e).mesh.SetId(meshRefId);
        REQUIRE(dscene::SaveScene(scene, *sceneInst).IsOk());
    }
    project->Settings().defaultSceneId = sceneId;
    project->Settings().defaultScene = String(u8"Scenes/Main");
    REQUIRE(project->SaveSettings().IsOk());

    ed::BuilderRegistry builders;
    builders.Register(UniquePtr<ed::IAssetBuilder>(
        DefaultAllocator().New<geo::StaticMeshAssetBuilder>(), DefaultAllocator()));
    ed::TemplateRegistry registry;
    SetupHostTemplate(toolDir.AsView(), registry);
    const ed::SceneReferenceScanner scanner = MakePruningScanner();

    // --- pruned export: only the reachable closure ships ---
    ed::ExportPreset preset;
    preset.name = String(u8"Pruned"); preset.platform = String(GetHostPlatformName());
    preset.outputSubdir = String(u8"pruned"); preset.pruneToReachable = true;
    ed::ExportResult result;
    REQUIRE(ed::ExportOne(*project, preset, registry, builders, outRoot.AsView(), false, &result,
                          {}, true, nullptr, &scanner).IsOk());

    draconic::vfs::PakFileSystem pak(PathJoin(result.outputDir.AsView(), proj::kDistContentPak).AsView());
    REQUIRE(pak.IsValid());
    draconic::content::ContentDatabase db(pak, BinarySerializerFactory(), proj::kCookedAssetExtension);
    CHECK(db.GetInstance(sceneId) != nullptr);      // scene staged
    CHECK(db.GetInstance(meshRefId) != nullptr);    // referenced mesh kept
    CHECK(db.GetInstance(meshDeadId) == nullptr);   // unreferenced mesh pruned

    // The report is loud: pruned, one default-scene root, the unreferenced mesh named as dropped.
    CHECK(result.pruning.pruned);
    REQUIRE(result.pruning.roots.Size() == 1u);
    CHECK(result.pruning.roots[0].reason == ed::ExportRootReason::DefaultScene);
    CHECK(result.pruning.roots[0].id == sceneId);
    bool droppedDead = false;
    for (const String& d : result.pruning.dropped) { if (d == u8"Meshes/Unreferenced") { droppedDead = true; } }
    CHECK(droppedDead);
    draconic::vfs::NativeFileSystem distFs(result.outputDir.AsView());
    CHECK(distFs.Exists(u8"export-report.txt"));    // report written beside the dist

    // --- non-pruned (default) export: EVERYTHING ships, no report (escape hatch, no regression) ---
    ed::ExportPreset full;
    full.name = String(u8"Full"); full.platform = String(GetHostPlatformName());
    full.outputSubdir = String(u8"full");   // pruneToReachable defaults false
    ed::ExportResult fullResult;
    REQUIRE(ed::ExportOne(*project, full, registry, builders, outRoot.AsView(), false, &fullResult,
                          {}, true, nullptr, &scanner).IsOk());
    draconic::vfs::PakFileSystem fullPak(
        PathJoin(fullResult.outputDir.AsView(), proj::kDistContentPak).AsView());
    REQUIRE(fullPak.IsValid());
    draconic::content::ContentDatabase fullDb(fullPak, BinarySerializerFactory(), proj::kCookedAssetExtension);
    CHECK(fullDb.GetInstance(meshRefId) != nullptr);
    CHECK(fullDb.GetInstance(meshDeadId) != nullptr);   // unreferenced ships when not pruning
    CHECK_FALSE(fullResult.pruning.pruned);
    draconic::vfs::NativeFileSystem fullFs(fullResult.outputDir.AsView());
    CHECK_FALSE(fullFs.Exists(u8"export-report.txt"));

    NukeTree(projectDir.AsView()); NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());
}

TEST_CASE("export: pruning keeps a scene -> prefab -> asset chain")
{
    GlobalTypeRegistry().Register(dscene::SceneDocument::StaticType());
    RegisterSerializable<dscene::SceneDocument>();
    GlobalTypeRegistry().Register(dscene::PrefabDocument::StaticType());
    RegisterSerializable<dscene::PrefabDocument>();
    geo::RegisterMeshAssets();
    GlobalTypeRegistry().Register(geo::StaticMeshSource::StaticType());
    RegisterSerializable<geo::StaticMeshSource>();

    const String projectDir = TempDir(u8"draconic_prunepf_proj");
    const String toolDir    = TempDir(u8"draconic_prunepf_tool");
    const String outRoot    = TempDir(u8"draconic_prunepf_out");
    NukeTree(projectDir.AsView()); NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());

    REQUIRE(ed::EditorProject::Create(projectDir.AsView(), u8"PrunePrefab").IsOk());
    UniquePtr<ed::EditorProject> project = ed::EditorProject::Open(projectDir.AsView());
    REQUIRE(static_cast<bool>(project));

    draconic::content::Group* meshes = project->SourceDb().RootGroup()->CreateGroup(u8"Meshes");
    const Guid meshInPrefab = AuthorMesh(*meshes, u8"PrefabMesh");
    const Guid meshDead     = AuthorMesh(*meshes, u8"Unreferenced");

    // The prefab body: an entity with a MeshComponent -> meshInPrefab, captured into the prefab
    // instance's "scene" stream. meshInPrefab is reachable ONLY through this prefab.
    draconic::content::Group* prefabsGroup = project->SourceDb().RootGroup()->CreateGroup(u8"Prefabs");
    draconic::content::Instance* prefabInst =
        prefabsGroup->CreateInstance(u8"Barrel", dscene::PrefabDocument::StaticType());
    REQUIRE(prefabInst != nullptr);
    { dscene::PrefabDocument doc; doc.name = String(u8"Barrel"); REQUIRE(prefabInst->WriteObject(doc).IsOk()); }
    const Guid prefabId = prefabInst->Id();
    {
        dscene::Scene author(u8"Barrel");
        author.AddSystem<draconic::render::MeshComponentManager>();
        const dscene::EntityHandle e = author.CreateEntity(u8"Body");
        author.GetSystem<draconic::render::MeshComponentManager>()->Add(e).mesh.SetId(meshInPrefab);
        MemoryStream payload;
        REQUIRE(dscene::CapturePrefab(author, e, payload).IsOk());
        const Span<const byte> bytes = payload.Bytes();
        REQUIRE(prefabInst->WriteData(u8"scene", bytes).IsOk());
    }

    // The main scene spawns the prefab, then saves it (persists as ref + deltas -> a
    // PendingPrefabInstance on load, NOT a flattened mesh).
    draconic::content::Group* scenes = project->SourceDb().RootGroup()->CreateGroup(u8"Scenes");
    draconic::content::Instance* sceneInst =
        scenes->CreateInstance(u8"Main", dscene::SceneDocument::StaticType());
    REQUIRE(sceneInst != nullptr);
    { dscene::SceneDocument doc; doc.name = String(u8"Main"); REQUIRE(sceneInst->WriteObject(doc).IsOk()); }
    const Guid sceneId = sceneInst->Id();
    {
        dscene::Scene scene(u8"Main");
        scene.AddSystem<draconic::render::MeshComponentManager>();
        UniquePtr<IStream> payloadStream = prefabInst->ReadData(u8"scene");
        REQUIRE(payloadStream.Get() != nullptr);
        const dscene::EntityHandle spawned = dscene::SpawnPrefab(scene, *payloadStream, prefabId);
        REQUIRE(spawned.IsAssigned());
        REQUIRE(scene.PrefabInstanceCount() == 1u);
        REQUIRE(dscene::SaveScene(scene, *sceneInst).IsOk());
    }
    project->Settings().defaultSceneId = sceneId;
    project->Settings().defaultScene = String(u8"Scenes/Main");
    REQUIRE(project->SaveSettings().IsOk());

    ed::BuilderRegistry builders;
    builders.Register(UniquePtr<ed::IAssetBuilder>(
        DefaultAllocator().New<geo::StaticMeshAssetBuilder>(), DefaultAllocator()));
    ed::TemplateRegistry registry;
    SetupHostTemplate(toolDir.AsView(), registry);
    const ed::SceneReferenceScanner scanner = MakePruningScanner();

    ed::ExportPreset preset;
    preset.name = String(u8"Pruned"); preset.platform = String(GetHostPlatformName());
    preset.outputSubdir = String(u8"pruned"); preset.pruneToReachable = true;
    ed::ExportResult result;
    REQUIRE(ed::ExportOne(*project, preset, registry, builders, outRoot.AsView(), false, &result,
                          {}, true, nullptr, &scanner).IsOk());

    // The whole chain is kept: scene staged, prefab staged (scene->prefab), the prefab's mesh cooked
    // in (prefab->asset); the unreferenced mesh is gone.
    draconic::vfs::PakFileSystem pak(PathJoin(result.outputDir.AsView(), proj::kDistContentPak).AsView());
    REQUIRE(pak.IsValid());
    draconic::content::ContentDatabase db(pak, BinarySerializerFactory(), proj::kCookedAssetExtension);
    CHECK(db.GetInstance(sceneId) != nullptr);
    CHECK(db.GetInstance(prefabId) != nullptr);       // prefab staged
    CHECK(db.GetInstance(meshInPrefab) != nullptr);   // reachable only through the prefab
    CHECK(db.GetInstance(meshDead) == nullptr);       // unreferenced dropped

    NukeTree(projectDir.AsView()); NukeTree(toolDir.AsView()); NukeTree(outRoot.AsView());
}
