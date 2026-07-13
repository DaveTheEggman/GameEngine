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
