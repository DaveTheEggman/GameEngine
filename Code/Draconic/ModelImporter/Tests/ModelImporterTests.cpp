// Draconic::ModelImporter tests - load a real glTF, cook it through the importer into a
// content DB, then bind the cooked ModelResource back through the resource manager and
// verify the whole convert -> cook -> bind chain (manifest nodes + resolved meshes).

#include "Core/Prelude.h"
#include <doctest/doctest.h>
#include <initializer_list>

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.animation;
import draconic.animation.resource;
import draconic.model;
import draconic.model.io;
import draconic.modelimporter;
import draconic.editor;
import draconic.editor.core;
import draconic.editor.cook;
import draconic.texture.editor;
import draconic.geometry.editor;
import draconic.materials.editor;
import draconic.animation.editor;
import draconic.materials;
import draconic.texture;
import draconic.texture.resource;
import draconic.rhi;
import draconic.rhi.null;
import draconic.materials.resource;

using namespace draconic::core;
namespace vfs = draconic::vfs;
namespace content  = draconic::content;
namespace resource = draconic::resource;
namespace geometry = draconic::geometry;
namespace model = draconic::model;
namespace modelimporter  = draconic::modelimporter;

#ifndef DRACONIC_MI_TEST_DUCK
#define DRACONIC_MI_TEST_DUCK ""
#endif
#ifndef DRACONIC_MI_TEST_FOX
#define DRACONIC_MI_TEST_FOX ""
#endif

TEST_CASE("import glTF -> cooked ModelResource round-trips through the resource system")
{
    const StringView duck(reinterpret_cast<const utf8char*>(DRACONIC_MI_TEST_DUCK));
    if (duck.IsEmpty()) { return; }   // path not configured (skip)

    modelimporter::RegisterModelImporterTypes();   // make the cooked types deserializable

    vfs::NativeFileSystem mount(u8"draconic_modelimporter_test_db");
    content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");

    // Cook the model file into the DB; get back the manifest (ModelResource) Guid.
    Guid modelGuid;
    const model::ModelLoadResult r = modelimporter::LoadAndCook(duck, db, u8"Duck", modelGuid);
    REQUIRE(r == model::ModelLoadResult::Ok);
    REQUIRE_FALSE(modelGuid.IsNil());

    // Bind the composite model: ModelFactory resolves its meshes via StaticMeshFactory.
    resource::ResourceManager manager(db);
    geometry::StaticMeshFactory meshFactory;
    modelimporter::ModelFactory       modelFactory;
    manager.AddFactory(&meshFactory);
    manager.AddFactory(&modelFactory);

    resource::Proxy<modelimporter::ModelResource> model = manager.Bind<modelimporter::ModelResource>(modelGuid);
    REQUIRE(model);
    CHECK(model->nodes.Size() > 0);
    CHECK(model->meshes.Size() > 0);

    // At least one node references a mesh, and that mesh resolved with real geometry.
    bool sawMesh = false;
    for (const modelimporter::ModelNode& n : model->nodes) {
        if (n.meshIndex >= 0 && static_cast<usize>(n.meshIndex) < model->meshes.Size()) {
            geometry::StaticMesh* mesh = model->meshes[static_cast<usize>(n.meshIndex)].Get();
            REQUIRE(mesh != nullptr);
            CHECK(mesh->VertexCount() > 0);
            CHECK(mesh->IndexCount() > 0);
            sawMesh = true;
        }
    }
    CHECK(sawMesh);
}

TEST_CASE("import skinned glTF -> cooked skeleton + animations + skinned mesh")
{
    const StringView fox(reinterpret_cast<const utf8char*>(DRACONIC_MI_TEST_FOX));
    if (fox.IsEmpty()) { return; }

    modelimporter::RegisterModelImporterTypes();

    vfs::NativeFileSystem mount(u8"draconic_modelimporter_fox_db");
    content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");

    Guid modelGuid;
    REQUIRE(modelimporter::LoadAndCook(fox, db, u8"Fox", modelGuid) == model::ModelLoadResult::Ok);

    resource::ResourceManager manager(db);
    geometry::StaticMeshFactory   meshFactory;
    geometry::SkinnedMeshFactory  skinnedFactory;
    modelimporter::ModelFactory         modelFactory;
    draconic::animation::SkeletonFactory      skeletonFactory;
    draconic::animation::AnimationClipFactory clipFactory;
    manager.AddFactory(&meshFactory);
    manager.AddFactory(&skinnedFactory);
    manager.AddFactory(&modelFactory);
    manager.AddFactory(&skeletonFactory);
    manager.AddFactory(&clipFactory);

    resource::Proxy<modelimporter::ModelResource> model = manager.Bind<modelimporter::ModelResource>(modelGuid);
    REQUIRE(model);

    // The Fox is skinned + animated: a resolved skeleton with bones + animation clips.
    REQUIRE(model->skeleton);
    CHECK(model->skeleton->BoneCount() > 0);
    CHECK(model->animations.Size() > 0);
    for (const auto& clip : model->animations) { REQUIRE(clip); CHECK(clip->duration > 0.0f); }

    // The mesh is flagged skinned + resolved as a SkinnedMesh (skin stream present).
    REQUIRE(model->meshes.Size() > 0);
    bool anySkinned = false;
    for (usize i = 0; i < model->meshSkinned.Size(); ++i) {
        if (model->meshSkinned[i] != 0) {
            anySkinned = true;
            geometry::StaticMesh* mesh = model->meshes[i].Get();
            REQUIRE(mesh != nullptr);
            CHECK(mesh->IsSkinned());
        }
    }
    CHECK(anySkinned);
}

// === Source-side file import (editor pipeline): GLB fan-out + full incremental cook ===

TEST_CASE("model-import: GLB fans out into source assets and cooks through the driver")
{
    using namespace draconic::editor;
    namespace mi = draconic::modelimporter;

    // Types the fan-out creates + their builders.
    mi::RegisterModelManifestAsset();
    draconic::texture::RegisterTextureAsset();
    draconic::geometry::RegisterMeshAssets();
    draconic::materials::RegisterMaterialAsset();
    draconic::animation::RegisterAnimationAssets();
    // Product types too (the test reads a cooked product back).
    GlobalTypeRegistry().Register(draconic::geometry::StaticMeshSource::StaticType());
    GlobalTypeRegistry().Register(draconic::geometry::SkinnedMeshSource::StaticType());
    RegisterSerializable<draconic::geometry::StaticMeshSource>();
    RegisterSerializable<draconic::geometry::SkinnedMeshSource>();

    const StringView dir = u8"draconic_model_import_project";
    auto cleanTree = [&]() {
        // Recursive best-effort cleanup of Content/Cooked/Sources/.cache trees.
        for (StringView sub : { u8"Content", u8"Cooked", u8"Sources", u8".cache" })
        {
            draconic::vfs::NativeFileSystem fs(PathJoin(dir, sub).AsView());
            Array<draconic::vfs::DirEntry> tops;
            if (fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                for (const auto& top : tops)
                {
                    if (!top.isDirectory) { (void)fs.AsWritable()->Delete(top.name.AsView()); continue; }
                    Array<draconic::vfs::DirEntry> inner;
                    if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                    {
                        for (const auto& e : inner)
                        {
                            (void)fs.AsWritable()->Delete(PathJoin(top.name.AsView(), e.name.AsView()).AsView());
                        }
                    }
                    (void)RemoveDirectory(PathJoin(PathJoin(dir, sub).AsView(), top.name.AsView()).AsView());
                }
            }
            (void)RemoveDirectory(PathJoin(dir, sub).AsView());
        }
        FileDelete(PathJoin(dir, u8"Project.xml"));
        (void)RemoveDirectory(PathJoin(dir, u8"Editor"));
        (void)RemoveDirectory(dir);
    };
    cleanTree();
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    // Import the Kenney character GLB (skinned: skeleton + clips expected).
    mi::ModelFileImporter importer;
    CHECK(importer.Accepts(u8"glb"));
    Result<draconic::content::Instance*> imported = importer.Import(
        reinterpret_cast<const draconic::core::utf8char*>(DRACONIC_MI_TEST_GLB),
        *project, *project->SourceDb().RootGroup());
    REQUIRE(imported.HasValue());
    draconic::content::Instance* manifestInst = imported.Value();
    REQUIRE(manifestInst != nullptr);
    CHECK(manifestInst->TypeName() == StringView(u8"ModelManifestAsset"));

    // The fan-out landed in a subgroup: meshes + a manifest at minimum.
    draconic::content::Group* modelGroup =
        project->SourceDb().RootGroup()->GetGroup(u8"character-oozi");
    REQUIRE(modelGroup != nullptr);
    CHECK(modelGroup->Instances().Size() >= 2u);

    RefPtr<ISerializable> object = manifestInst->ReadObject();
    auto* manifestAsset = Cast<mi::ModelManifestAsset>(object.Get());
    REQUIRE(manifestAsset != nullptr);
    REQUIRE(manifestAsset->manifest.meshGuids.Size() >= 1u);
    CHECK(manifestAsset->manifest.nodes.Size() >= 1u);

    // Cook EVERYTHING through the incremental driver (the real pipeline path).
    BuilderRegistry builders;
    auto add = [&](auto* builder) {
        builders.Register(UniquePtr<IAssetBuilder>(builder, DefaultAllocator()));
    };
    add(DefaultAllocator().New<draconic::texture::TextureAssetBuilder>());
    add(DefaultAllocator().New<draconic::geometry::StaticMeshAssetBuilder>());
    add(DefaultAllocator().New<draconic::geometry::SkinnedMeshAssetBuilder>());
    add(DefaultAllocator().New<draconic::materials::MaterialAssetBuilder>());
    add(DefaultAllocator().New<draconic::animation::SkeletonAssetBuilder>());
    add(DefaultAllocator().New<draconic::animation::AnimationClipAssetBuilder>());
    add(DefaultAllocator().New<mi::ModelManifestAssetBuilder>());

    draconic::vfs::NativeFileSystem sourcesFs(project->SourcesRoot().AsView());
    draconic::vfs::NativeFileSystem cacheFs(project->CacheRoot().AsView());
    CookDriver driver(project->SourceDb(), project->CookedDb(), builders, &sourcesFs, &cacheFs);

    CookPlan plan = driver.Plan();
    CHECK(plan.dirty.Size() >= 2u);   // manifest + meshes (+ any materials/skeleton/clips)
    CookStats stats = driver.Execute(plan);
    CHECK(stats.failed == 0u);
    CHECK(stats.cooked == plan.dirty.Size());
    CHECK(driver.Plan().dirty.IsEmpty());   // incremental: everything clean now

    // Products landed under the source guids: the manifest product + a bindable mesh.
    CHECK(project->CookedDb().GetInstance(manifestInst->Id()) != nullptr);
    const Guid meshGuid = manifestAsset->manifest.meshGuids[0];
    RefPtr<ISerializable> meshProduct = project->CookedDb().ReadObject(meshGuid);
    REQUIRE(meshProduct.Get() != nullptr);

    cleanTree();
}

// Regression (user-reported): dropping a .gltf with EXTERNAL sidecars (Fox.bin, Texture.png)
// failed - the importer loaded from the Sources/ copy where the sidecars don't exist. It now
// loads from the original path and copies the referenced sidecars into Sources/.
TEST_CASE("model-import: external-sidecar .gltf imports and its sidecars land in Sources")
{
    using namespace draconic::editor;
    namespace mi = draconic::modelimporter;

    mi::RegisterModelManifestAsset();
    draconic::texture::RegisterTextureAsset();
    draconic::geometry::RegisterMeshAssets();
    draconic::materials::RegisterMaterialAsset();
    draconic::animation::RegisterAnimationAssets();

    const StringView dir = u8"draconic_gltf_import_project";
    auto cleanTree = [&]() {
        for (StringView sub : { u8"Content", u8"Cooked", u8"Sources", u8".cache" })
        {
            draconic::vfs::NativeFileSystem fs(PathJoin(dir, sub).AsView());
            Array<draconic::vfs::DirEntry> tops;
            if (fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                for (const auto& top : tops)
                {
                    if (!top.isDirectory) { (void)fs.AsWritable()->Delete(top.name.AsView()); continue; }
                    Array<draconic::vfs::DirEntry> inner;
                    if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                    {
                        for (const auto& e : inner)
                        {
                            (void)fs.AsWritable()->Delete(PathJoin(top.name.AsView(), e.name.AsView()).AsView());
                        }
                    }
                    (void)RemoveDirectory(PathJoin(PathJoin(dir, sub).AsView(), top.name.AsView()).AsView());
                }
            }
            (void)RemoveDirectory(PathJoin(dir, sub).AsView());
        }
        FileDelete(PathJoin(dir, u8"Project.xml"));
        (void)RemoveDirectory(PathJoin(dir, u8"Editor"));
        (void)RemoveDirectory(dir);
    };
    cleanTree();
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    mi::ModelFileImporter importer;
    Result<draconic::content::Instance*> imported = importer.Import(
        reinterpret_cast<const draconic::core::utf8char*>(DRACONIC_MI_TEST_FOX),
        *project, *project->SourceDb().RootGroup());
    REQUIRE(imported.HasValue());
    REQUIRE(imported.Value() != nullptr);

    // Main file + both referenced sidecars are in Sources/.
    CHECK(FileExists(PathJoin(dir, u8"Sources/Fox.gltf").AsView()));
    CHECK(FileExists(PathJoin(dir, u8"Sources/Fox.bin").AsView()));
    CHECK(FileExists(PathJoin(dir, u8"Sources/Texture.png").AsView()));

    // The fan-out produced meshes + the Fox's animation clips.
    RefPtr<ISerializable> object = imported.Value()->ReadObject();
    auto* manifest = Cast<mi::ModelManifestAsset>(object.Get());
    REQUIRE(manifest != nullptr);
    CHECK(manifest->manifest.meshGuids.Size() >= 1u);
    CHECK(manifest->manifest.animationGuids.Size() >= 1u);   // Survey/Walk/Run

    cleanTree();
}

// Regression (user-reported): a freshly imported model's material rendered UNTEXTURED when
// picked directly (the Sandbox spawn-composite path wired textures; the self-contained
// MaterialSource path must too). Full chain: import -> cook -> bind material -> the albedo
// texture is installed as the material's default.
TEST_CASE("model-import: a bound material carries its albedo texture")
{
    using namespace draconic::editor;
    namespace mi = draconic::modelimporter;
    namespace res = draconic::resource;

    mi::RegisterModelManifestAsset();
    mi::RegisterModelImporterTypes();
    draconic::texture::RegisterTextureAsset();
    draconic::geometry::RegisterMeshAssets();
    draconic::materials::RegisterMaterialAsset();
    draconic::animation::RegisterAnimationAssets();

    const StringView dir = u8"draconic_mat_tex_project";
    auto cleanTree = [&]() {
        for (StringView sub : { u8"Content", u8"Cooked", u8"Sources", u8".cache" })
        {
            draconic::vfs::NativeFileSystem fs(PathJoin(dir, sub).AsView());
            Array<draconic::vfs::DirEntry> tops;
            if (fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                for (const auto& top : tops)
                {
                    if (!top.isDirectory) { (void)fs.AsWritable()->Delete(top.name.AsView()); continue; }
                    Array<draconic::vfs::DirEntry> inner;
                    if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                    {
                        for (const auto& e : inner)
                        {
                            (void)fs.AsWritable()->Delete(PathJoin(top.name.AsView(), e.name.AsView()).AsView());
                        }
                    }
                    (void)RemoveDirectory(PathJoin(PathJoin(dir, sub).AsView(), top.name.AsView()).AsView());
                }
            }
            (void)RemoveDirectory(PathJoin(dir, sub).AsView());
        }
        FileDelete(PathJoin(dir, u8"Project.xml"));
        (void)RemoveDirectory(PathJoin(dir, u8"Editor"));
        (void)RemoveDirectory(dir);
    };
    cleanTree();
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    // Import the Duck (textured, static) + cook everything.
    mi::ModelFileImporter importer;
    Result<draconic::content::Instance*> imported = importer.Import(
        reinterpret_cast<const draconic::core::utf8char*>(DRACONIC_MI_TEST_DUCK),
        *project, *project->SourceDb().RootGroup());
    REQUIRE(imported.HasValue());

    RefPtr<ISerializable> object = imported.Value()->ReadObject();
    auto* manifest = Cast<mi::ModelManifestAsset>(object.Get());
    REQUIRE(manifest != nullptr);
    REQUIRE(manifest->manifest.materialGuids.Size() >= 1u);
    const Guid matGuid = manifest->manifest.materialGuids[0];

    BuilderRegistry builders;
    auto add = [&](auto* builder) {
        builders.Register(UniquePtr<IAssetBuilder>(builder, DefaultAllocator()));
    };
    add(DefaultAllocator().New<draconic::texture::TextureAssetBuilder>());
    add(DefaultAllocator().New<draconic::geometry::StaticMeshAssetBuilder>());
    add(DefaultAllocator().New<draconic::geometry::SkinnedMeshAssetBuilder>());
    add(DefaultAllocator().New<draconic::materials::MaterialAssetBuilder>());
    add(DefaultAllocator().New<draconic::animation::SkeletonAssetBuilder>());
    add(DefaultAllocator().New<draconic::animation::AnimationClipAssetBuilder>());
    add(DefaultAllocator().New<mi::ModelManifestAssetBuilder>());

    draconic::vfs::NativeFileSystem sourcesFs(project->SourcesRoot().AsView());
    draconic::vfs::NativeFileSystem cacheFs(project->CacheRoot().AsView());
    CookDriver driver(project->SourceDb(), project->CookedDb(), builders, &sourcesFs, &cacheFs);
    CookPlan plan = driver.Plan();
    CookStats stats = driver.Execute(plan);
    REQUIRE(stats.failed == 0u);

    // Bind the material like the editor does (null GPU device backs the texture factory).
    // Device FIRST: it must outlive the manager's cached products (their destructors release
    // GPU objects through it).
    draconic::rhi::null::NullDevice device{DefaultAllocator()};
    res::ResourceManager resources(project->CookedDb());
    draconic::geometry::StaticMeshFactory meshFactory;
    draconic::materials::MaterialFactory materialFactory;
    draconic::texture::TextureFactory textureFactory(device);
    resources.AddFactory(&meshFactory);
    resources.AddFactory(&materialFactory);
    resources.AddFactory(&textureFactory);

    res::Proxy<draconic::materials::Material> material =
        resources.Bind<draconic::materials::Material>(matGuid);
    REQUIRE(static_cast<bool>(material));

    // The albedo default texture must be installed on the AlbedoMap property.
    i32 albedoIndex = -1;
    const auto props = material->Properties();
    for (usize i = 0; i < props.Size(); ++i)
    {
        if (props[i].name == StringView(u8"AlbedoMap")) { albedoIndex = static_cast<i32>(i); }
    }
    REQUIRE(albedoIndex >= 0);
    CHECK(material->GetDefaultTexture(static_cast<usize>(albedoIndex)) != nullptr);

    cleanTree();
}
