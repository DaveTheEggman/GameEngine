// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::ModelImporter tests - load a real glTF, cook it through the importer into a
// content DB, then bind the cooked ModelResource back through the resource manager and
// verify the whole convert -> cook -> bind chain (manifest nodes + resolved meshes).

#include "Core/Prelude.h"
#include <doctest/doctest.h>
#include <initializer_list>

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.geometry;
import foundation.geometry.resource;
import foundation.animation;
import foundation.animation.resource;
import foundation.model;
import foundation.model.io;
import modelimporter;
import physics.pipeline;
import pipeline.core;
import editor.core;
import pipeline.importer;
import pipeline.cook;

using namespace pipeline;
import texture.pipeline;
import geometry.pipeline;
import materials.pipeline;
import animation.pipeline;
import foundation.materials;
import foundation.texture;
import foundation.texture.resource;
import foundation.rhi;
import foundation.rhi.null;
import foundation.materials.resource;

using namespace foundation::core;
namespace vfs = foundation::vfs;
namespace content = foundation::content;
namespace resource = foundation::resource;
namespace geometry = foundation::geometry;
namespace model = foundation::model;

#ifndef TEST_MI_DUCK
#define TEST_MI_DUCK ""
#endif
#ifndef TEST_MI_FOX
#define TEST_MI_FOX ""
#endif

TEST_CASE("import glTF -> cooked ModelResource round-trips through the resource system")
{
    const StringView duck(reinterpret_cast<const utf8char*>(TEST_MI_DUCK));
    if (duck.IsEmpty())
    {
        return;
    } // path not configured (skip)

    model::RegisterModelResourceTypes(); // make the cooked types deserializable

    vfs::NativeFileSystem mount(u8"scratch_modelimporter_test_db", DefaultAllocator());
    content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::core::BinarySerializerFactory(), u8".rasset");

    // Cook the model file into the DB; get back the manifest (ModelResource) Guid.
    Guid modelGuid;
    const model::ModelLoadResult r = pipeline::LoadAndCook(duck, db, u8"Duck", modelGuid);
    REQUIRE(r == model::ModelLoadResult::Ok);
    REQUIRE_FALSE(modelGuid.IsNil());

    // Bind the composite model: ModelFactory resolves its meshes via StaticMeshFactory.
    resource::ResourceManager manager(DefaultAllocator(), db);
    geometry::StaticMeshFactory meshFactory(DefaultAllocator());
    model::ModelFactory modelFactory;
    manager.AddFactory(&meshFactory);
    manager.AddFactory(&modelFactory);

    resource::Proxy<model::ModelResource> model = manager.Bind<model::ModelResource>(modelGuid);
    REQUIRE(model);
    CHECK(model->nodes.Size() > 0);
    CHECK(model->meshes.Size() > 0);

    // At least one node references a mesh, and that mesh resolved with real geometry.
    bool sawMesh = false;
    for (const pipeline::ModelNode& n : model->nodes)
    {
        if (n.meshIndex >= 0 && static_cast<usize>(n.meshIndex) < model->meshes.Size())
        {
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
    const StringView fox(reinterpret_cast<const utf8char*>(TEST_MI_FOX));
    if (fox.IsEmpty())
    {
        return;
    }

    model::RegisterModelResourceTypes();

    vfs::NativeFileSystem mount(u8"scratch_modelimporter_fox_db", DefaultAllocator());
    content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::core::BinarySerializerFactory(), u8".rasset");

    Guid modelGuid;
    REQUIRE(pipeline::LoadAndCook(fox, db, u8"Fox", modelGuid) == model::ModelLoadResult::Ok);

    resource::ResourceManager manager(DefaultAllocator(), db);
    geometry::StaticMeshFactory meshFactory(DefaultAllocator());
    geometry::SkinnedMeshFactory skinnedFactory(DefaultAllocator());
    model::ModelFactory modelFactory;
    foundation::animation::SkeletonFactory skeletonFactory(DefaultAllocator());
    foundation::animation::AnimationClipFactory clipFactory(DefaultAllocator());
    manager.AddFactory(&meshFactory);
    manager.AddFactory(&skinnedFactory);
    manager.AddFactory(&modelFactory);
    manager.AddFactory(&skeletonFactory);
    manager.AddFactory(&clipFactory);

    resource::Proxy<model::ModelResource> model = manager.Bind<model::ModelResource>(modelGuid);
    REQUIRE(model);

    // The Fox is skinned + animated: a resolved skeleton with bones + animation clips.
    REQUIRE(model->skeleton);
    CHECK(model->skeleton->BoneCount() > 0);
    CHECK(model->animations.Size() > 0);
    for (const auto& clip : model->animations)
    {
        REQUIRE(clip);
        CHECK(clip->duration > 0.0f);
    }

    // The mesh is flagged skinned + resolved as a SkinnedMesh (skin stream present).
    REQUIRE(model->meshes.Size() > 0);
    bool anySkinned = false;
    for (usize i = 0; i < model->meshSkinned.Size(); ++i)
    {
        if (model->meshSkinned[i] != 0)
        {
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
    using namespace editor;
    using namespace pipeline;

    // Types the fan-out creates + their builders.
    pipeline::RegisterModelManifestAsset();
    pipeline::RegisterTextureAsset();
    pipeline::RegisterMeshAssets();
    pipeline::RegisterMaterialAsset();
    pipeline::RegisterAnimationAssets();
    // Product types too (the test reads a cooked product back).
    GlobalTypeRegistry().Register(foundation::geometry::StaticMeshSource::StaticType());
    GlobalTypeRegistry().Register(foundation::geometry::SkinnedMeshSource::StaticType());
    RegisterSerializable<foundation::geometry::StaticMeshSource>();
    RegisterSerializable<foundation::geometry::SkinnedMeshSource>();

    const StringView dir = u8"scratch_model_import_project";
    auto cleanTree = [&]()
    {
        // Recursive best-effort cleanup of Content/Cooked/Sources/.cache trees.
        for (StringView sub : {u8"Content", u8"Cooked", u8"Sources", u8".cache"})
        {
            foundation::vfs::NativeFileSystem fs(PathJoin(dir, sub).AsView(), foundation::core::DefaultAllocator());
            Array<foundation::vfs::DirEntry> tops;
            if (fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                for (const auto& top : tops)
                {
                    if (!top.isDirectory)
                    {
                        (void)fs.AsWritable()->Delete(top.name.AsView());
                        continue;
                    }
                    Array<foundation::vfs::DirEntry> inner;
                    if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                    {
                        for (const auto& e : inner)
                        {
                            (void)fs.AsWritable()->Delete(
                                PathJoin(top.name.AsView(), e.name.AsView()).AsView());
                        }
                    }
                    (void)RemoveDirectory(
                        PathJoin(PathJoin(dir, sub).AsView(), top.name.AsView()).AsView());
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
    pipeline::ModelFileImporter importer;
    CHECK(importer.Accepts(u8"glb"));
    Result<foundation::content::Instance*> imported =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_GLB),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(imported.HasValue());
    foundation::content::Instance* manifestInst = imported.Value();
    REQUIRE(manifestInst != nullptr);
    CHECK(manifestInst->TypeName() == StringView(u8"ModelManifestAsset"));

    // The fan-out produces a subgroup: meshes + a manifest at minimum.
    foundation::content::Group* modelGroup =
        project->SourceDb().RootGroup()->GetGroup(u8"character-oozi");
    REQUIRE(modelGroup != nullptr);
    CHECK(modelGroup->Instances().Size() >= 2u);

    RefPtr<ISerializable> object = manifestInst->ReadObject();
    auto* manifestAsset = Cast<pipeline::ModelManifestAsset>(object.Get());
    REQUIRE(manifestAsset != nullptr);
    REQUIRE(manifestAsset->manifest.meshGuids.Size() >= 1u);
    CHECK(manifestAsset->manifest.nodes.Size() >= 1u);

    // Cook EVERYTHING through the incremental driver (the real pipeline path).
    BuilderRegistry builders;
    auto add = [&](auto* builder)
    { builders.Register(UniquePtr<IAssetBuilder>(builder, DefaultAllocator())); };
    add(DefaultAllocator().New<pipeline::TextureAssetBuilder>());
    add(DefaultAllocator().New<pipeline::StaticMeshAssetBuilder>());
    add(DefaultAllocator().New<pipeline::SkinnedMeshAssetBuilder>());
    add(DefaultAllocator().New<pipeline::MaterialAssetBuilder>());
    add(DefaultAllocator().New<pipeline::SkeletonAssetBuilder>());
    add(DefaultAllocator().New<pipeline::AnimationClipAssetBuilder>());
    add(DefaultAllocator().New<pipeline::ModelManifestAssetBuilder>());

    foundation::vfs::NativeFileSystem sourcesFs(project->SourcesRoot().AsView(), foundation::core::DefaultAllocator());
    foundation::vfs::NativeFileSystem cacheFs(project->CacheRoot().AsView(), foundation::core::DefaultAllocator());
    CookDriver driver(project->SourceDb(), project->CookedDb(), builders, &sourcesFs, &cacheFs);

    CookPlan plan = driver.Plan();
    CHECK(plan.dirty.Size() >= 2u); // manifest + meshes (+ any materials/skeleton/clips)
    CookStats stats = driver.Execute(plan);
    CHECK(stats.failed == 0u);
    CHECK(stats.cooked == plan.dirty.Size());
    CHECK(driver.Plan().dirty.IsEmpty()); // incremental: everything clean now

    // Products land under the source guids: the manifest product + a bindable mesh.
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
    using namespace editor;

    pipeline::RegisterModelManifestAsset();
    pipeline::RegisterTextureAsset();
    pipeline::RegisterMeshAssets();
    pipeline::RegisterMaterialAsset();
    pipeline::RegisterAnimationAssets();

    const StringView dir = u8"scratch_gltf_import_project";
    auto cleanTree = [&]()
    {
        for (StringView sub : {u8"Content", u8"Cooked", u8"Sources", u8".cache"})
        {
            foundation::vfs::NativeFileSystem fs(PathJoin(dir, sub).AsView(), foundation::core::DefaultAllocator());
            Array<foundation::vfs::DirEntry> tops;
            if (fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                for (const auto& top : tops)
                {
                    if (!top.isDirectory)
                    {
                        (void)fs.AsWritable()->Delete(top.name.AsView());
                        continue;
                    }
                    Array<foundation::vfs::DirEntry> inner;
                    if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                    {
                        for (const auto& e : inner)
                        {
                            (void)fs.AsWritable()->Delete(
                                PathJoin(top.name.AsView(), e.name.AsView()).AsView());
                        }
                    }
                    (void)RemoveDirectory(
                        PathJoin(PathJoin(dir, sub).AsView(), top.name.AsView()).AsView());
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

    pipeline::ModelFileImporter importer;
    Result<foundation::content::Instance*> imported =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_FOX),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(imported.HasValue());
    REQUIRE(imported.Value() != nullptr);

    // Main file + both referenced sidecars are in Sources/.
    CHECK(FileExists(PathJoin(dir, u8"Sources/Fox.gltf").AsView()));
    CHECK(FileExists(PathJoin(dir, u8"Sources/Fox.bin").AsView()));
    CHECK(FileExists(PathJoin(dir, u8"Sources/Texture.png").AsView()));

    // The fan-out produced meshes + the Fox's animation clips.
    RefPtr<ISerializable> object = imported.Value()->ReadObject();
    auto* manifest = Cast<pipeline::ModelManifestAsset>(object.Get());
    REQUIRE(manifest != nullptr);
    CHECK(manifest->manifest.meshGuids.Size() >= 1u);
    CHECK(manifest->manifest.animationGuids.Size() >= 1u); // Survey/Walk/Run

    cleanTree();
}

// Regression (user-reported): a freshly imported model's material rendered UNTEXTURED when
// picked directly (the Sandbox spawn-composite path wired textures; the self-contained
// MaterialSource path must too). Full chain: import -> cook -> bind material -> the albedo
// texture is installed as the material's default.
TEST_CASE("model-import: a bound material carries its albedo texture")
{
    using namespace editor;

    pipeline::RegisterModelManifestAsset();
    foundation::model::RegisterModelResourceTypes();
    pipeline::RegisterTextureAsset();
    pipeline::RegisterMeshAssets();
    pipeline::RegisterMaterialAsset();
    pipeline::RegisterAnimationAssets();

    const StringView dir = u8"scratch_mat_tex_project";
    auto cleanTree = [&]()
    {
        for (StringView sub : {u8"Content", u8"Cooked", u8"Sources", u8".cache"})
        {
            foundation::vfs::NativeFileSystem fs(PathJoin(dir, sub).AsView(), foundation::core::DefaultAllocator());
            Array<foundation::vfs::DirEntry> tops;
            if (fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                for (const auto& top : tops)
                {
                    if (!top.isDirectory)
                    {
                        (void)fs.AsWritable()->Delete(top.name.AsView());
                        continue;
                    }
                    Array<foundation::vfs::DirEntry> inner;
                    if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                    {
                        for (const auto& e : inner)
                        {
                            (void)fs.AsWritable()->Delete(
                                PathJoin(top.name.AsView(), e.name.AsView()).AsView());
                        }
                    }
                    (void)RemoveDirectory(
                        PathJoin(PathJoin(dir, sub).AsView(), top.name.AsView()).AsView());
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
    pipeline::ModelFileImporter importer;
    Result<foundation::content::Instance*> imported =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_DUCK),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(imported.HasValue());

    RefPtr<ISerializable> object = imported.Value()->ReadObject();
    auto* manifest = Cast<pipeline::ModelManifestAsset>(object.Get());
    REQUIRE(manifest != nullptr);
    REQUIRE(manifest->manifest.materialGuids.Size() >= 1u);
    const Guid matGuid = manifest->manifest.materialGuids[0];

    BuilderRegistry builders;
    auto add = [&](auto* builder)
    { builders.Register(UniquePtr<IAssetBuilder>(builder, DefaultAllocator())); };
    add(DefaultAllocator().New<pipeline::TextureAssetBuilder>());
    add(DefaultAllocator().New<pipeline::StaticMeshAssetBuilder>());
    add(DefaultAllocator().New<pipeline::SkinnedMeshAssetBuilder>());
    add(DefaultAllocator().New<pipeline::MaterialAssetBuilder>());
    add(DefaultAllocator().New<pipeline::SkeletonAssetBuilder>());
    add(DefaultAllocator().New<pipeline::AnimationClipAssetBuilder>());
    add(DefaultAllocator().New<pipeline::ModelManifestAssetBuilder>());

    foundation::vfs::NativeFileSystem sourcesFs(project->SourcesRoot().AsView(), foundation::core::DefaultAllocator());
    foundation::vfs::NativeFileSystem cacheFs(project->CacheRoot().AsView(), foundation::core::DefaultAllocator());
    CookDriver driver(project->SourceDb(), project->CookedDb(), builders, &sourcesFs, &cacheFs);
    CookPlan plan = driver.Plan();
    CookStats stats = driver.Execute(plan);
    REQUIRE(stats.failed == 0u);

    // Bind the material like the editor does (null GPU device backs the texture factory).
    // Device FIRST: it must outlive the manager's cached products (their destructors release
    // GPU objects through it).
    foundation::rhi::null::NullDevice device{DefaultAllocator()};
    resource::ResourceManager resources(DefaultAllocator(), project->CookedDb());
    foundation::geometry::StaticMeshFactory meshFactory(DefaultAllocator());
    foundation::materials::MaterialFactory materialFactory;
    foundation::texture::TextureFactory textureFactory(DefaultAllocator(), device);
    resources.AddFactory(&meshFactory);
    resources.AddFactory(&materialFactory);
    resources.AddFactory(&textureFactory);

    resource::Proxy<foundation::materials::Material> material =
        resources.Bind<foundation::materials::Material>(matGuid);
    REQUIRE(static_cast<bool>(material));

    // The albedo default texture must be installed on the AlbedoMap property.
    i32 albedoIndex = -1;
    const auto props = material->Properties();
    for (usize i = 0; i < props.Size(); ++i)
    {
        if (props[i].name == StringView(u8"AlbedoMap"))
        {
            albedoIndex = static_cast<i32>(i);
        }
    }
    REQUIRE(albedoIndex >= 0);
    CHECK(material->GetDefaultTexture(static_cast<usize>(albedoIndex)) != nullptr);

    cleanTree();
}

TEST_CASE("importer: FBX separate metal/rough maps bake into one packed MR texture")
{
    // Two 2x2 grayscale sources: roughness = 200, metalness = 60. The packed result must be
    // glTF-convention (G = roughness, B = metalness; R = A = 255) - feeding either source
    // directly into the packed slot would bleed its value across BOTH channels.
    model::Model mdl;
    const auto makeGray = [&](u8 value) -> i32
    {
        auto* tex = new model::ModelTexture();
        u8 px[2 * 2 * 4];
        for (u32 i = 0; i < 4; ++i)
        {
            px[i * 4] = value;
            px[i * 4 + 1] = value;
            px[i * 4 + 2] = value;
            px[i * 4 + 3] = 255;
        }
        tex->width = 2;
        tex->height = 2;
        tex->setData(static_cast<const u8*>(px), static_cast<usize>(sizeof(px)));
        return mdl.addTexture(tex);
    };
    const i32 roughIdx = makeGray(200);
    const i32 metalIdx = makeGray(60);

    u32 w = 0, h = 0;
    Array<u8> packed =
        pipeline::BakePackedMetallicRoughness(mdl, roughIdx, metalIdx, w, h);
    REQUIRE(packed.Size() == 2u * 2u * 4u);
    CHECK(w == 2u);
    CHECK(h == 2u);
    CHECK(packed[0] == 255u); // R unused
    CHECK(packed[1] == 200u); // G = roughness
    CHECK(packed[2] == 60u);  // B = metalness
    CHECK(packed[3] == 255u);

    // A missing map bakes identity (255) so the scalar factor carries the value.
    Array<u8> roughOnly =
        pipeline::BakePackedMetallicRoughness(mdl, roughIdx, -1, w, h);
    REQUIRE(roughOnly.Size() == 2u * 2u * 4u);
    CHECK(roughOnly[1] == 200u);
    CHECK(roughOnly[2] == 255u);

    // Usage classification: both sources are data maps -> LINEAR.
    Array<bool> linear;
    auto* mat = new model::ModelMaterial();
    mat->separateRoughnessTextureIndex = roughIdx;
    mat->separateMetalnessTextureIndex = metalIdx;
    mdl.addMaterial(mat);
    pipeline::ClassifyLinearTextures(mdl, linear);
    REQUIRE(linear.Size() == 2u);
    CHECK(linear[0]);
    CHECK(linear[1]);
}

TEST_CASE("mesh convert: missing tangent stream generates tangents (DamagedHelmet class)")
{
    // A quad in the XY plane, normal +Z, with U mapped along +Y - so the generated tangent
    // must be ~(0,1,0), NOT the {1,0,0} default a missing stream would otherwise leave behind.
    struct SrcVertex
    {
        Float3 pos;
        Float3 normal;
        Float2 uv;
    };
    const SrcVertex verts[4] = {
        {{0, 0, 0}, {0, 0, 1}, {0, 0}},
        {{1, 0, 0}, {0, 0, 1}, {0, 1}},
        {{1, 1, 0}, {0, 0, 1}, {1, 1}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0}},
    };
    const u32 indices[6] = {0, 1, 2, 0, 2, 3};

    model::ModelMesh mesh;
    mesh.addVertexElement(model::VertexElement(model::VertexSemantic::Position,
                                               model::VertexElementFormat::Float3, 0));
    mesh.addVertexElement(model::VertexElement(model::VertexSemantic::Normal,
                                               model::VertexElementFormat::Float3, 12));
    mesh.addVertexElement(model::VertexElement(model::VertexSemantic::TexCoord,
                                               model::VertexElementFormat::Float2, 24));
    mesh.allocateVertices(4, sizeof(SrcVertex));
    mesh.setVertexData(verts, 4);
    mesh.allocateIndices(6, true);
    mesh.setIndexData(indices, 6);

    geometry::StaticMeshSource source;
    pipeline::StaticMeshSourceFromModel(mesh, source);
    REQUIRE(source.vertexBlob.Size() == 4 * sizeof(geometry::StaticMeshVertex));
    const auto* out = reinterpret_cast<const geometry::StaticMeshVertex*>(source.vertexBlob.Data());
    for (usize i = 0; i < 4; ++i)
    {
        const Float4 t = out[i].tangent;
        CHECK(Abs(t.y) == doctest::Approx(1.0f).epsilon(0.01)); // U runs along +Y
        CHECK(Abs(t.x) < 0.01f);
        CHECK(Abs(t.z) < 0.01f);
        // This U-along-Y mapping is MIRRORED: B = dP/dv = +X but cross(N,T) = -X, so the
        // generated handedness must be -1 (also proves w isn't stuck at the +1 default).
        CHECK(t.w == doctest::Approx(-1.0f));
        CHECK(Abs(Dot(Float3{t.x, t.y, t.z}, out[i].normal)) < 0.01f);
    }

    // An AUTHORED tangent stream passes through untouched (no regeneration).
    struct SrcVertexT
    {
        Float3 pos;
        Float3 normal;
        Float2 uv;
        Float4 tangent;
    };
    const Float4 authored{0, 0, 1, -1};
    SrcVertexT tverts[4];
    for (usize i = 0; i < 4; ++i)
    {
        tverts[i] = {verts[i].pos, verts[i].normal, verts[i].uv, authored};
    }
    model::ModelMesh tmesh;
    tmesh.addVertexElement(model::VertexElement(model::VertexSemantic::Position,
                                                model::VertexElementFormat::Float3, 0));
    tmesh.addVertexElement(model::VertexElement(model::VertexSemantic::Normal,
                                                model::VertexElementFormat::Float3, 12));
    tmesh.addVertexElement(model::VertexElement(model::VertexSemantic::TexCoord,
                                                model::VertexElementFormat::Float2, 24));
    tmesh.addVertexElement(model::VertexElement(model::VertexSemantic::Tangent,
                                                model::VertexElementFormat::Float4, 32));
    tmesh.allocateVertices(4, sizeof(SrcVertexT));
    tmesh.setVertexData(tverts, 4);
    tmesh.allocateIndices(6, true);
    tmesh.setIndexData(indices, 6);

    geometry::StaticMeshSource tsource;
    pipeline::StaticMeshSourceFromModel(tmesh, tsource);
    const auto* tout =
        reinterpret_cast<const geometry::StaticMeshVertex*>(tsource.vertexBlob.Data());
    for (usize i = 0; i < 4; ++i)
    {
        CHECK(tout[i].tangent.z == doctest::Approx(1.0f));
        CHECK(tout[i].tangent.w == doctest::Approx(-1.0f));
    }
}

// Regression (user-reported): delete a model's group -> reimport the same file -> recook ->
// the editor crashed with garbage vkCreateImage params (a texture product misparsed). The
// suspicious seam: EnsureProduct adopts a same-named cooked instance from the PREVIOUS
// generation via CreateInstanceWithId's return-existing-by-name behavior, breaking the
// "product guid == source guid" invariant. This walks the exact user flow at DB level.
TEST_CASE("cook: delete group -> reimport -> recook keeps product identities clean")
{
    using namespace editor;

    pipeline::RegisterModelManifestAsset();
    pipeline::RegisterTextureAsset();
    pipeline::RegisterMeshAssets();
    pipeline::RegisterMaterialAsset();
    pipeline::RegisterAnimationAssets();
    // Product type registration (the test reads a texture product back).
    GlobalTypeRegistry().Register(foundation::texture::TextureResource::StaticType());
    RegisterSerializable<foundation::texture::TextureResource>();

    const StringView dir = u8"scratch_reimport_identity_project";
    auto cleanTree = [&]()
    {
        for (StringView sub : {u8"Content", u8"Cooked", u8"Sources", u8".cache"})
        {
            foundation::vfs::NativeFileSystem fs(PathJoin(dir, sub).AsView(), foundation::core::DefaultAllocator());
            Array<foundation::vfs::DirEntry> tops;
            if (fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                for (const auto& top : tops)
                {
                    if (!top.isDirectory)
                    {
                        (void)fs.AsWritable()->Delete(top.name.AsView());
                        continue;
                    }
                    Array<foundation::vfs::DirEntry> inner;
                    if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                    {
                        for (const auto& e : inner)
                        {
                            (void)fs.AsWritable()->Delete(
                                PathJoin(top.name.AsView(), e.name.AsView()).AsView());
                        }
                    }
                    (void)RemoveDirectory(
                        PathJoin(PathJoin(dir, sub).AsView(), top.name.AsView()).AsView());
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

    BuilderRegistry builders;
    auto add = [&](auto* builder)
    { builders.Register(UniquePtr<IAssetBuilder>(builder, DefaultAllocator())); };
    add(DefaultAllocator().New<pipeline::TextureAssetBuilder>());
    add(DefaultAllocator().New<pipeline::StaticMeshAssetBuilder>());
    add(DefaultAllocator().New<pipeline::SkinnedMeshAssetBuilder>());
    add(DefaultAllocator().New<pipeline::MaterialAssetBuilder>());
    add(DefaultAllocator().New<pipeline::SkeletonAssetBuilder>());
    add(DefaultAllocator().New<pipeline::AnimationClipAssetBuilder>());
    add(DefaultAllocator().New<pipeline::ModelManifestAssetBuilder>());

    foundation::vfs::NativeFileSystem sourcesFs(project->SourcesRoot().AsView(), foundation::core::DefaultAllocator());
    foundation::vfs::NativeFileSystem cacheFs(project->CacheRoot().AsView(), foundation::core::DefaultAllocator());
    CookDriver driver(project->SourceDb(), project->CookedDb(), builders, &sourcesFs, &cacheFs);

    // Duck has a texture (the crashing product kind). Import + cook generation 1.
    pipeline::ModelFileImporter importer;
    Result<foundation::content::Instance*> firstImport =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_DUCK),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(firstImport.HasValue());

    foundation::content::Group* duckGroup = project->SourceDb().RootGroup()->GetGroup(u8"Duck");
    REQUIRE(duckGroup != nullptr);
    Array<Guid> oldGuids;
    for (foundation::content::Instance* inst : duckGroup->Instances())
    {
        oldGuids.PushBack(inst->Id());
    }
    const usize assetCount = oldGuids.Size();
    REQUIRE(assetCount >= 3u); // manifest + mesh + material + texture

    CookPlan plan1 = driver.Plan();
    CookStats stats1 = driver.Execute(plan1);
    CHECK(stats1.failed == 0u);
    for (const Guid& id : oldGuids)
    {
        CHECK(project->CookedDb().GetInstance(id) != nullptr);
    }

    // === user step 1: delete the group (source side only - DeleteGroupNow's behavior) ===
    REQUIRE(project->SourceDb().DeleteGroup(*duckGroup).IsOk());

    // === user step 2: reimport the same file ===
    Result<foundation::content::Instance*> secondImport =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_DUCK),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(secondImport.HasValue());
    foundation::content::Group* duckGroup2 = project->SourceDb().RootGroup()->GetGroup(u8"Duck");
    REQUIRE(duckGroup2 != nullptr);
    Array<Guid> newGuids;
    for (foundation::content::Instance* inst : duckGroup2->Instances())
    {
        newGuids.PushBack(inst->Id());
    }
    REQUIRE(newGuids.Size() == assetCount);

    // === user step 3: the next cook (sweeps generation-1 orphans, cooks generation 2) ===
    CookPlan plan2 = driver.Plan();
    CHECK(plan2.orphans.Size() == assetCount); // EVERY dead source must be an orphan
    CookStats stats2 = driver.Execute(plan2);
    CHECK(stats2.failed == 0u);

    // Identity invariant: every generation-2 product exists UNDER ITS SOURCE GUID and
    // matches its source's name; no generation-1 product survives.
    for (usize i = 0; i < newGuids.Size(); ++i)
    {
        const Guid& id = newGuids[i];
        const bool isNew = [&]
        {
            for (const Guid& g : oldGuids)
            {
                if (g == id)
                    return false;
            }
            return true;
        }();
        CHECK(isNew); // reimport minted fresh guids (otherwise this test tests nothing)
        foundation::content::Instance* product = project->CookedDb().GetInstance(id);
        REQUIRE(product != nullptr);
        foundation::content::Instance* source = project->SourceDb().GetInstance(id);
        REQUIRE(source != nullptr);
        CHECK(product->Name() == source->Name());
    }
    for (const Guid& id : oldGuids)
    {
        CHECK(project->CookedDb().GetInstance(id) == nullptr);
    }

    // Identity-guard regression (the guid-replay crash): plant a STALE same-named product
    // with a foreign guid and a CROSS-TYPED product under a real source guid, then force a
    // recook - EnsureProduct must remove both and land every product on source guid + type.
    {
        foundation::content::Group* cookedDuck = project->CookedDb().RootGroup()->GetGroup(u8"Duck");
        REQUIRE(cookedDuck != nullptr);
        // (a) name collision: same name as a real product, different guid.
        foundation::content::Instance* real = nullptr;
        for (const Guid& id : newGuids)
        {
            if (foundation::content::Instance* p = project->CookedDb().GetInstance(id))
            {
                real = p;
                break;
            }
        }
        REQUIRE(real != nullptr);
        const String collidedName(real->Name());
        const Guid foreign = Guid{0xDEADBEEFull, 0xFEEDF00Dull};
        (void)project->CookedDb().DeleteInstance(real->Id());
        foundation::content::Instance* stale = cookedDuck->CreateInstanceWithId(
            foreign, collidedName.AsView(), foundation::texture::TextureResource::StaticType());
        REQUIRE(stale != nullptr);
        CHECK(stale->Id() == foreign);

        CookPlan replan = driver.Plan(true); // force: every asset re-cooks
        CookStats restats = driver.Execute(replan);
        CHECK(restats.failed == 0u);
        for (const Guid& id : newGuids)
        {
            foundation::content::Instance* product = project->CookedDb().GetInstance(id);
            REQUIRE(product != nullptr);
            foundation::content::Instance* source = project->SourceDb().GetInstance(id);
            REQUIRE(source != nullptr);
            CHECK(product->Name() == source->Name());
        }
        CHECK(project->CookedDb().GetInstance(foreign) == nullptr); // stale removed
    }

    // Scoped plans: PlanFor(roots) covers the roots + their dependency closure ONLY.
    {
        Guid matGuid, meshGuid;
        for (foundation::content::Instance* inst : duckGroup2->Instances())
        {
            if (inst->TypeName() == StringView(u8"MaterialAsset"))
            {
                matGuid = inst->Id();
            }
            else if (inst->TypeName() == StringView(u8"StaticMeshAsset"))
            {
                meshGuid = inst->Id();
            }
        }
        REQUIRE(!matGuid.IsNil());
        REQUIRE(!meshGuid.IsNil());

        // Everything is clean after the full cook: a scoped un-forced plan is empty.
        Guid meshRoots[] = {meshGuid};
        CookPlan clean = driver.PlanFor(Span<const Guid>{meshRoots, 1});
        CHECK(clean.dirty.IsEmpty());
        CHECK(clean.orphans.IsEmpty()); // scoped plans never sweep

        // Force re-cooks the ROOT only; its clean dependency closure (textures) stays out.
        Guid matRoots[] = {matGuid};
        CookPlan forced = driver.PlanFor(Span<const Guid>{matRoots, 1}, true);
        REQUIRE(forced.dirty.Size() == 1u);
        CHECK(forced.dirty[0].source == matGuid);
        CookStats scopedStats = driver.Execute(forced);
        CHECK(scopedStats.failed == 0u);
        CHECK(scopedStats.cooked == 1u);
    }

    // The texture product must deserialize to a SANE TextureResource (the crash showed
    // garbage width/height/mips from a misparsed product).
    bool textureChecked = false;
    for (const Guid& id : newGuids)
    {
        foundation::content::Instance* product = project->CookedDb().GetInstance(id);
        if (product == nullptr || product->TypeName() != StringView(u8"TextureResource"))
        {
            continue;
        }
        RefPtr<ISerializable> object = project->CookedDb().ReadObject(id);
        auto* tex = Cast<foundation::texture::TextureResource>(object.Get());
        REQUIRE(tex != nullptr);
        CHECK(tex->width > 0u);
        CHECK(tex->width < 65536u);
        CHECK(tex->height > 0u);
        CHECK(tex->height < 65536u);
        CHECK(tex->mipLevels > 0u);
        textureChecked = true;
    }
    CHECK(textureChecked);

    cleanTree();
}

TEST_CASE("import: texture assets keep their source names")
{
    model::ModelTexture named;
    named.setName(u8"BaseColor");
    CHECK(pipeline::ImportedTextureName(named, 0).AsView() == StringView(u8"BaseColor"));

    model::ModelTexture fromUri;
    fromUri.setUri(u8"textures/Default_albedo.jpg");
    CHECK(pipeline::ImportedTextureName(fromUri, 3).AsView() ==
          StringView(u8"Default_albedo"));

    model::ModelTexture bare; // embedded, no identity -> indexed fallback
    CHECK(pipeline::ImportedTextureName(bare, 7).AsView() == StringView(u8"tex.7"));
}

TEST_CASE("import: material sampler modes map from the source texture's sampler")
{
    CHECK(pipeline::AddressModeFromWrap(model::TextureWrap::Repeat) == 0);
    CHECK(pipeline::AddressModeFromWrap(model::TextureWrap::MirroredRepeat) == 1);
    CHECK(pipeline::AddressModeFromWrap(model::TextureWrap::ClampToEdge) == 2);
}

TEST_CASE("import: sub-assets keep authored names (sanitized), indexed fallback otherwise")
{
    CHECK(pipeline::ImportedAssetName(u8"Material_MR", u8"mat", 0).AsView() ==
          StringView(u8"Material_MR"));
    CHECK(pipeline::ImportedAssetName(u8"mesh_helmet_LP", u8"mesh", 3).AsView() ==
          StringView(u8"mesh_helmet_LP"));
    // Path-hostile characters sanitize (names become envelope file names).
    CHECK(pipeline::ImportedAssetName(u8"body/armor:v2", u8"mesh", 0).AsView() ==
          StringView(u8"body_armor_v2"));
    // No authored name -> indexed fallback.
    CHECK(pipeline::ImportedAssetName(u8"", u8"anim", 4).AsView() == StringView(u8"anim.4"));
}

namespace
{
    // Recursive best-effort cleanup so EditorProject::Create starts from a clean slate
    // across test runs (same shape as the first test's cleanTree lambda, two levels deep).
    void CleanProjectTree(StringView dir)
    {
        for (StringView sub : {u8"Content", u8"Cooked", u8"Sources", u8".cache"})
        {
            foundation::vfs::NativeFileSystem fs(foundation::core::PathJoin(dir, sub).AsView(), foundation::core::DefaultAllocator());
            foundation::core::Array<foundation::vfs::DirEntry> tops;
            if (!fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                continue;
            }
            for (const auto& top : tops)
            {
                if (!top.isDirectory)
                {
                    (void)fs.AsWritable()->Delete(top.name.AsView());
                    continue;
                }
                foundation::core::Array<foundation::vfs::DirEntry> inner;
                if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                {
                    for (const auto& e : inner)
                    {
                        foundation::core::String path =
                            foundation::core::PathJoin(top.name.AsView(), e.name.AsView());
                        (void)fs.AsWritable()->Delete(path.AsView());
                    }
                }
                (void)fs.AsWritable()->Delete(top.name.AsView());
            }
            (void)foundation::core::RemoveDirectory(foundation::core::PathJoin(dir, sub).AsView());
        }
        foundation::vfs::NativeFileSystem fs(dir, foundation::core::DefaultAllocator());
        foundation::core::Array<foundation::vfs::DirEntry> entries;
        if (fs.AsEnumerable()->Enumerate(u8"", entries).IsOk())
        {
            for (const auto& e : entries)
            {
                if (!e.isDirectory)
                {
                    (void)fs.AsWritable()->Delete(e.name.AsView());
                }
            }
        }
        (void)foundation::core::RemoveDirectory(dir);
    }
}

TEST_CASE("model-import: options gate textures/materials/animations")
{
    using namespace editor;
    pipeline::RegisterModelManifestAsset();
    pipeline::RegisterTextureAsset();
    pipeline::RegisterMeshAssets();
    pipeline::RegisterMaterialAsset();
    pipeline::RegisterAnimationAssets();

    const StringView dir = u8"scratch_model_import_options_project";
    CleanProjectTree(dir);
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    pipeline::ModelFileImporter importer;

    // The importer advertises options, and their defaults import everything.
    RefPtr<ImportOptions> base = importer.CreateOptions();
    REQUIRE(base.Get() != nullptr);
    auto* options = static_cast<pipeline::ModelImportOptions*>(base.Get());
    CHECK(options->importTextures);
    CHECK(options->importMaterials);
    CHECK(options->importAnimations);
    CHECK(options->generatePrefab);
    CHECK_FALSE(options->generateScene); // opt-in
    CHECK_FALSE(options->generateCollision); // opt-in
    CHECK_FALSE(options->collisionConvex);
    CHECK(options->Toggles().Size() == 8u); // +Generate LODs

    // Geometry-only import: no textures, no materials, no skeleton/clips in the fan-out.
    options->importTextures = false;
    options->importMaterials = false;
    options->importAnimations = false;
    Result<foundation::content::Instance*> imported =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_GLB),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), options, nullptr, nullptr);
    REQUIRE(imported.HasValue());

    RefPtr<ISerializable> object = imported.Value()->ReadObject();
    auto* manifest = Cast<pipeline::ModelManifestAsset>(object.Get());
    REQUIRE(manifest != nullptr);
    CHECK(manifest->manifest.meshGuids.Size() >= 1u); // geometry always imports
    CHECK(manifest->manifest.materialGuids.IsEmpty());
    CHECK(manifest->manifest.skeletonGuid.IsNil());
    CHECK(manifest->manifest.animationGuids.IsEmpty());

    foundation::content::Group* modelGroup =
        project->SourceDb().RootGroup()->GetGroup(u8"character-oozi");
    REQUIRE(modelGroup != nullptr);
    for (foundation::content::Instance* inst : modelGroup->Instances())
    {
        CHECK(inst->TypeName() != StringView(u8"TextureAsset"));
        CHECK(inst->TypeName() != StringView(u8"MaterialAsset"));
        CHECK(inst->TypeName() != StringView(u8"SkeletonAsset"));
        CHECK(inst->TypeName() != StringView(u8"AnimationClipAsset"));
    }
}

TEST_CASE("model-import: generate-collision emits CollisionShapeAssets wired to the meshes")
{
    using namespace editor;
    pipeline::RegisterModelManifestAsset();
    pipeline::RegisterTextureAsset();
    pipeline::RegisterMeshAssets();
    pipeline::RegisterMaterialAsset();
    pipeline::RegisterAnimationAssets();
    pipeline::RegisterPhysicsAssets();

    const StringView dir = u8"scratch_model_import_collision_project";
    CleanProjectTree(dir);
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    pipeline::ModelFileImporter importer;
    RefPtr<ImportOptions> base = importer.CreateOptions();
    auto* options = static_cast<pipeline::ModelImportOptions*>(base.Get());
    options->generateCollision = true;
    options->collisionConvex = true;
    Result<foundation::content::Instance*> imported =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_GLB),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), options, nullptr, nullptr);
    REQUIRE(imported.HasValue());

    RefPtr<ISerializable> object = imported.Value()->ReadObject();
    auto* manifest = Cast<pipeline::ModelManifestAsset>(object.Get());
    REQUIRE(manifest != nullptr);
    // collisionGuids parallels meshGuids; STATIC meshes get shapes (skinned stay nil).
    REQUIRE(manifest->manifest.collisionGuids.Size() == manifest->manifest.meshGuids.Size());
    usize shapeCount = 0;
    for (usize i = 0; i < manifest->manifest.collisionGuids.Size(); ++i)
    {
        const Guid& g = manifest->manifest.collisionGuids[i];
        if (manifest->manifest.meshSkinned[i] != 0)
        {
            CHECK(g.IsNil());
            continue;
        }
        REQUIRE(!g.IsNil());
        ++shapeCount;
        foundation::content::Instance* inst = project->SourceDb().GetInstance(g);
        REQUIRE(inst != nullptr);
        RefPtr<ISerializable> shapeObject = inst->ReadObject();
        auto* shape = Cast<pipeline::CollisionShapeAsset>(shapeObject.Get());
        REQUIRE(shape != nullptr);
        CHECK(shape->sourceMesh == manifest->manifest.meshGuids[i]);
        CHECK(shape->cook == pipeline::CollisionCookKind::ConvexHull);
    }
    const bool anySkinned = [&]
    {
        for (u8 skinned : manifest->manifest.meshSkinned)
        {
            if (skinned != 0)
            {
                return true;
            }
        }
        return false;
    }();
    CHECK((shapeCount > 0 || anySkinned));
}

TEST_CASE("model-import: re-import WITHOUT delete reuses instances (same guids, no duplicates)")
{
    using namespace editor;
    pipeline::RegisterModelManifestAsset();
    pipeline::RegisterTextureAsset();
    pipeline::RegisterMeshAssets();
    pipeline::RegisterMaterialAsset();
    pipeline::RegisterAnimationAssets();

    const StringView dir = u8"scratch_model_reimport_project";
    CleanProjectTree(dir);
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    pipeline::ModelFileImporter importer;
    Result<foundation::content::Instance*> first =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_DUCK),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(first.HasValue());

    foundation::content::Group* duck = project->SourceDb().RootGroup()->GetGroup(u8"Duck");
    REQUIRE(duck != nullptr);
    HashMap<String, Guid> before;
    for (foundation::content::Instance* inst : duck->Instances())
    {
        before.InsertOrAssign(String(inst->Name()), inst->Id());
    }
    const usize assetCount = before.Size();
    REQUIRE(assetCount >= 3u);

    // Re-drop the SAME file with the group intact: every instance is REUSED by (name, type) -
    // guids survive (placed refs + the prefab keep working) and nothing duplicates as ".2".
    Result<foundation::content::Instance*> second =
        importer.Import(reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_DUCK),
                        pipeline::ImportContext{project->SourcesRoot()}, *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
    REQUIRE(second.HasValue());
    CHECK(second.Value()->Id() == first.Value()->Id());

    foundation::content::Group* duckAfter = project->SourceDb().RootGroup()->GetGroup(u8"Duck");
    REQUIRE(duckAfter != nullptr);
    CHECK(duckAfter->Instances().Size() == assetCount); // no duplicates
    for (foundation::content::Instance* inst : duckAfter->Instances())
    {
        const Guid* old = before.Find(String(inst->Name()));
        REQUIRE(old != nullptr);   // same name set as the first import
        CHECK(*old == inst->Id()); // same guid: reuse, not re-mint
    }
}

TEST_CASE("mesh lod: _LODn suffix parsing (case-insensitive; _LOD0 and non-suffixes stay plain)")
{
    String base;
    CHECK(pipeline::ParseLodSuffix(u8"Foo_LOD1", base) == 1);
    CHECK(base == StringView(u8"Foo"));
    CHECK(pipeline::ParseLodSuffix(u8"Rock_lod2", base) == 2);
    CHECK(base == StringView(u8"Rock"));
    CHECK(pipeline::ParseLodSuffix(u8"Wall_Lod12", base) == 12);
    CHECK(base == StringView(u8"Wall"));
    CHECK(pipeline::ParseLodSuffix(u8"Foo", base) == 0);
    CHECK(pipeline::ParseLodSuffix(u8"Foo_LOD0", base) == 0);  // the base names itself plainly
    CHECK(pipeline::ParseLodSuffix(u8"Foo_LOD", base) == 0);   // no digits
    CHECK(pipeline::ParseLodSuffix(u8"LOD1", base) == 0);      // no underscore/base
    CHECK(pipeline::ParseLodSuffix(u8"Foo_MOD1", base) == 0);  // wrong tag
}

TEST_CASE("mesh lod: authored level appends into the base's chain (offset indices, ranges, defaults)")
{
    struct SrcVertex
    {
        Float3 pos;
        Float3 normal;
        Float2 uv;
    };
    const auto makeQuad = [](model::ModelMesh& mesh, f32 xShift, u32 vertexCount, u32 indexCount,
                             const u32* indices)
    {
        Array<SrcVertex> verts;
        for (u32 i = 0; i < vertexCount; ++i)
        {
            verts.PushBack(SrcVertex{Float3{xShift + static_cast<f32>(i), 0, 0}, Float3{0, 0, 1},
                                     Float2{0, 0}});
        }
        mesh.addVertexElement(model::VertexElement(model::VertexSemantic::Position,
                                                   model::VertexElementFormat::Float3, 0));
        mesh.addVertexElement(model::VertexElement(model::VertexSemantic::Normal,
                                                   model::VertexElementFormat::Float3, 12));
        mesh.addVertexElement(model::VertexElement(model::VertexSemantic::TexCoord,
                                                   model::VertexElementFormat::Float2, 24));
        mesh.allocateVertices(static_cast<i32>(vertexCount), sizeof(SrcVertex));
        mesh.setVertexData(verts.Data(), static_cast<i32>(vertexCount));
        mesh.allocateIndices(static_cast<i32>(indexCount), true);
        mesh.setIndexData(indices, static_cast<i32>(indexCount));
    };

    const u32 baseIndices[6] = {0, 1, 2, 0, 2, 3};
    model::ModelMesh baseMesh;
    makeQuad(baseMesh, 0.0f, 4, 6, baseIndices);
    geometry::StaticMeshSource source;
    pipeline::StaticMeshSourceFromModel(baseMesh, source);
    REQUIRE(source.subStart.Size() == 1);

    const u32 lodIndices[3] = {0, 1, 2};
    model::ModelMesh lodMesh;
    makeQuad(lodMesh, 100.0f, 3, 3, lodIndices);
    REQUIRE(pipeline::AppendLodLevelFromModel(lodMesh, source));

    // Chain shape: 2 levels, level 1 = one range after the base's 6 indices, indices
    // offset past the base's 4 vertices, default threshold ladder (LOD1 at 0.25).
    CHECK(source.lodCount == 2);
    REQUIRE(source.lodStart.Size() == 1);
    CHECK(source.lodStart[0] == 6);
    CHECK(source.lodIndexCount[0] == 3);
    CHECK(source.vertexBlob.Size() == 7 * sizeof(geometry::StaticMeshVertex));
    CHECK(source.indexData.Size() == 9);
    CHECK(source.indexData[6] == 4); // 0 + 4-vertex offset
    REQUIRE(source.lodCoverage.Size() == 2);
    CHECK(source.lodCoverage[1] == doctest::Approx(0.25f));

    // The level's vertices really are the appended ones (position x carries the shift).
    const auto* verts =
        reinterpret_cast<const geometry::StaticMeshVertex*>(source.vertexBlob.Data());
    CHECK(verts[4].position.x == doctest::Approx(100.0f));

    // A second level extends the ladder.
    model::ModelMesh lod2;
    const u32 lod2Indices[3] = {0, 2, 1};
    makeQuad(lod2, 200.0f, 3, 3, lod2Indices);
    REQUIRE(pipeline::AppendLodLevelFromModel(lod2, source));
    CHECK(source.lodCount == 3);
    CHECK(source.lodCoverage[2] == doctest::Approx(0.125f));

    // Part-count mismatch: refused, base untouched.
    model::ModelMesh twoParts;
    const u32 tpIndices[6] = {0, 1, 2, 0, 2, 1};
    makeQuad(twoParts, 300.0f, 3, 6, tpIndices);
    twoParts.addPart(model::ModelMeshPart{0, 3, 0});
    twoParts.addPart(model::ModelMeshPart{3, 3, 1});
    const usize blobBefore = source.vertexBlob.Size();
    CHECK_FALSE(pipeline::AppendLodLevelFromModel(twoParts, source));
    CHECK(source.vertexBlob.Size() == blobBefore);
    CHECK(source.lodCount == 3);

    // The chain survives the runtime fill (levels slice; LOD 1 draws its own range).
    geometry::StaticMesh mesh;
    source.FillStatic(mesh);
    CHECK(mesh.lodCount == 3);
    CHECK(mesh.SubMeshesForLod(1)[0].startIndex == 6);
    CHECK(mesh.SubMeshesForLod(1)[0].indexCount == 3);
}

TEST_CASE("mesh lod: a skinned level appends with its skinning stream in lockstep")
{
    struct SkinVertex
    {
        Float3 pos;
        Float3 normal;
        Float2 uv;
        u16 joints[4];
        Float4 weights;
    };
    const auto makeSkinnedTri = [](model::ModelMesh& mesh, f32 xShift, u16 joint)
    {
        SkinVertex verts[3];
        for (u32 i = 0; i < 3; ++i)
        {
            verts[i] = {};
            verts[i].pos = Float3{xShift + static_cast<f32>(i), 0, 0};
            verts[i].normal = Float3{0, 0, 1};
            verts[i].joints[0] = joint;
            verts[i].weights = Float4{1, 0, 0, 0};
        }
        const u32 indices[3] = {0, 1, 2};
        mesh.addVertexElement(model::VertexElement(model::VertexSemantic::Position,
                                                   model::VertexElementFormat::Float3, 0));
        mesh.addVertexElement(model::VertexElement(model::VertexSemantic::Normal,
                                                   model::VertexElementFormat::Float3, 12));
        mesh.addVertexElement(model::VertexElement(model::VertexSemantic::TexCoord,
                                                   model::VertexElementFormat::Float2, 24));
        mesh.addVertexElement(model::VertexElement(model::VertexSemantic::Joints,
                                                   model::VertexElementFormat::UShort4, 32));
        mesh.addVertexElement(model::VertexElement(model::VertexSemantic::Weights,
                                                   model::VertexElementFormat::Float4, 40));
        mesh.allocateVertices(3, sizeof(SkinVertex));
        mesh.setVertexData(verts, 3);
        mesh.allocateIndices(3, true);
        mesh.setIndexData(indices, 3);
    };

    model::ModelMesh baseMesh;
    makeSkinnedTri(baseMesh, 0.0f, 7);
    geometry::SkinnedMeshSource source;
    pipeline::SkinnedMeshSourceFromModel(baseMesh, 2, source);
    REQUIRE(source.skinningBlob.Size() == 3 * sizeof(geometry::VertexSkinning));

    model::ModelMesh lodMesh;
    makeSkinnedTri(lodMesh, 50.0f, 9);
    REQUIRE(pipeline::AppendLodLevelFromModel(lodMesh, source));

    // Both streams grew by the level's 3 vertices; the level's entries carry joint 9.
    CHECK(source.lodCount == 2);
    CHECK(source.vertexBlob.Size() == 6 * sizeof(geometry::StaticMeshVertex));
    CHECK(source.skinningBlob.Size() == 6 * sizeof(geometry::VertexSkinning));
    const auto* skin =
        reinterpret_cast<const geometry::VertexSkinning*>(source.skinningBlob.Data());
    CHECK(skin[2].joints[0] == 7); // base entries intact
    CHECK(skin[3].joints[0] == 9); // appended level entries
    CHECK(source.lodStart[0] == 3);
    CHECK(source.indexData[3] == 3); // offset past the base's vertices
}

TEST_CASE("model-import: DescribeImport lists the fan-out; the selection filters and renames it")
{
    using namespace editor;
    using namespace pipeline;
    pipeline::RegisterModelManifestAsset();
    pipeline::RegisterTextureAsset();
    pipeline::RegisterMeshAssets();
    pipeline::RegisterMaterialAsset();
    pipeline::RegisterAnimationAssets();

    const StringView dir = u8"scratch_model_describe_project";
    auto cleanTree = [&]()
    {
        for (StringView sub : {u8"Content", u8"Cooked", u8"Sources", u8".cache"})
        {
            foundation::vfs::NativeFileSystem fs(PathJoin(dir, sub).AsView(), foundation::core::DefaultAllocator());
            Array<foundation::vfs::DirEntry> tops;
            if (fs.AsEnumerable()->Enumerate(u8"", tops).IsOk())
            {
                for (const auto& top : tops)
                {
                    if (!top.isDirectory)
                    {
                        (void)fs.AsWritable()->Delete(top.name.AsView());
                        continue;
                    }
                    Array<foundation::vfs::DirEntry> inner;
                    if (fs.AsEnumerable()->Enumerate(top.name.AsView(), inner).IsOk())
                    {
                        for (const auto& e : inner)
                        {
                            (void)fs.AsWritable()->Delete(
                                PathJoin(top.name.AsView(), e.name.AsView()).AsView());
                        }
                    }
                    (void)RemoveDirectory(
                        PathJoin(PathJoin(dir, sub).AsView(), top.name.AsView()).AsView());
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

    pipeline::ModelFileImporter importer;
    const auto* glb = reinterpret_cast<const foundation::core::utf8char*>(TEST_MI_GLB);

    // The plan of a skinned character: meshes + materials + skeleton + clips, all enabled,
    // target = source name.
    pipeline::ImportPlan plan = importer.DescribeImport(glb, nullptr, nullptr);
    REQUIRE(!plan.IsEmpty());
    usize meshCount = 0, clipCount = 0, skeletonCount = 0;
    for (const pipeline::ImportPlanEntry& e : plan.entries)
    {
        CHECK(e.enabled);
        CHECK(e.targetName.AsView() == e.sourceName.AsView());
        meshCount += (e.kind == pipeline::ImportResourceKind::Mesh) ? 1u : 0u;
        clipCount += (e.kind == pipeline::ImportResourceKind::AnimationClip) ? 1u : 0u;
        skeletonCount += (e.kind == pipeline::ImportResourceKind::Skeleton) ? 1u : 0u;
    }
    REQUIRE(meshCount >= 1u);
    REQUIRE(clipCount >= 1u);
    REQUIRE(skeletonCount == 1u);

    // PARITY: an unfiltered import creates an instance for EVERY plan entry, by name.
    {
        Result<foundation::content::Instance*> imported = importer.Import(
            glb, pipeline::ImportContext{project->SourcesRoot()},
            *project->SourceDb().RootGroup(), nullptr, nullptr, nullptr);
        REQUIRE(imported.HasValue());
        foundation::content::Group& modelGroup = imported.Value()->OwningGroup();
        for (const pipeline::ImportPlanEntry& e : plan.entries)
        {
            CHECK(modelGroup.GetInstance(e.sourceName.AsView()) != nullptr);
        }
    }

    // Selection: clips OFF, first mesh RENAMED - the fan-out skips and renames accordingly.
    {
        auto options = MakeRef<pipeline::ModelImportOptions>(DefaultAllocator());
        options->selection = importer.DescribeImport(glb, options.Get(), nullptr);
        String meshSource;
        for (pipeline::ImportPlanEntry& e : options->selection.entries)
        {
            if (e.kind == pipeline::ImportResourceKind::AnimationClip)
            {
                e.enabled = false;
            }
            if (e.kind == pipeline::ImportResourceKind::Mesh && meshSource.IsEmpty())
            {
                meshSource = e.sourceName;
                e.targetName = String(u8"hero.mesh");
            }
        }
        REQUIRE(!meshSource.IsEmpty());

        foundation::content::Group* target =
            project->SourceDb().RootGroup()->CreateGroup(u8"filtered");
        Result<foundation::content::Instance*> imported = importer.Import(
            glb, pipeline::ImportContext{project->SourcesRoot()}, *target, options.Get(),
            nullptr, nullptr);
        REQUIRE(imported.HasValue());
        foundation::content::Group& modelGroup = imported.Value()->OwningGroup();

        CHECK(modelGroup.GetInstance(u8"hero.mesh") != nullptr);   // renamed
        CHECK(modelGroup.GetInstance(meshSource.AsView()) == nullptr); // not under the old name
        for (foundation::content::Instance* inst : modelGroup.Instances())
        {
            CHECK(inst->TypeName() != StringView(u8"AnimationClipAsset")); // clips skipped
        }
        // The manifest kept the skeleton (still selected) and lost the clips.
        RefPtr<ISerializable> object = imported.Value()->ReadObject();
        auto* manifest = Cast<pipeline::ModelManifestAsset>(object.Get());
        REQUIRE(manifest != nullptr);
        CHECK(!manifest->manifest.skeletonGuid.IsNil());
        CHECK(manifest->manifest.animationGuids.IsEmpty());
        CHECK(!manifest->manifest.meshGuids.IsEmpty());
        CHECK(!manifest->manifest.meshGuids[0].IsNil());

        // RE-IMPORT MEMORY: the manifest stored the decisions; StoredSelection reads them
        // back, and merging onto a fresh plan reproduces them - clips stay off, the rename
        // sticks, without the user re-answering anything.
        pipeline::ImportPlan stored = importer.StoredSelection(*target, glb);
        REQUIRE(!stored.IsEmpty());
        pipeline::ImportPlan fresh = importer.DescribeImport(glb, nullptr, nullptr);
        pipeline::MergeStoredSelection(fresh, stored);
        bool sawRenamedMesh = false;
        for (const pipeline::ImportPlanEntry& e : fresh.entries)
        {
            if (e.kind == pipeline::ImportResourceKind::AnimationClip)
            {
                CHECK(!e.enabled);
            }
            if (e.sourceName == meshSource)
            {
                sawRenamedMesh = true;
                CHECK(e.targetName == u8"hero.mesh");
            }
        }
        CHECK(sawRenamedMesh);
    }

    cleanTree();
}

TEST_CASE("model-import: LOD folding holds the manifest slot so node mesh indices stay valid")
{
    // Regression: meshes [Part_LOD1, Part] folded the level OUT of manifest.meshGuids while
    // node.meshIndex kept the MODEL index - the Part node's index 1 fell out of the 1-entry
    // array and the generated prefab silently lost the mesh.
    using namespace editor;
    using namespace pipeline;
    pipeline::RegisterModelManifestAsset();
    pipeline::RegisterMeshAssets();

    const StringView dir = u8"scratch_model_lodslot_project";
    FileDelete(PathJoin(dir, u8"Content/lodtest/Part.xasset"));
    FileDelete(PathJoin(dir, u8"Content/lodtest/Part.geometry.bin"));
    FileDelete(PathJoin(dir, u8"Content/lodtest/lodtest.xasset"));
    (void)RemoveDirectory(PathJoin(dir, u8"Content/lodtest"));
    FileDelete(PathJoin(dir, u8"Sources/lodtest.glb"));
    for (StringView sub : {u8"Content", u8"Cooked", u8"Sources", u8".cache", u8"Editor"})
    {
        (void)RemoveDirectory(PathJoin(dir, sub));
    }
    FileDelete(PathJoin(dir, u8"Project.xml"));
    (void)RemoveDirectory(dir);
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    // The dropped file only needs to EXIST (provenance copy); the parsed model arrives as
    // the prepared payload, exactly like the editor's worker-prepare path.
    const String fakeSource = PathJoin(dir, u8"lodtest.glb");
    const byte junk[4] = {byte{1}, byte{2}, byte{3}, byte{4}};
    REQUIRE(WriteFile(fakeSource.AsView(), Span<const byte>(junk, 4)).IsOk());

    struct SrcVertex
    {
        Float3 pos;
        Float3 normal;
        Float2 uv;
    };
    const SrcVertex verts[3] = {
        {{0, 0, 0}, {0, 0, 1}, {0, 0}},
        {{1, 0, 0}, {0, 0, 1}, {0, 1}},
        {{1, 1, 0}, {0, 0, 1}, {1, 1}},
    };
    const u32 indices[3] = {0, 1, 2};
    const auto makeMesh = [&](StringView name) -> model::ModelMesh*
    {
        auto* mesh = new model::ModelMesh();
        mesh->setName(name);
        mesh->addVertexElement(model::VertexElement(model::VertexSemantic::Position,
                                                    model::VertexElementFormat::Float3, 0));
        mesh->addVertexElement(model::VertexElement(model::VertexSemantic::Normal,
                                                    model::VertexElementFormat::Float3, 12));
        mesh->addVertexElement(model::VertexElement(model::VertexSemantic::TexCoord,
                                                    model::VertexElementFormat::Float2, 24));
        mesh->allocateVertices(3, sizeof(SrcVertex));
        mesh->setVertexData(verts, 3);
        mesh->allocateIndices(3, true);
        mesh->setIndexData(indices, 3);
        return mesh;
    };

    auto prepared = MakeRef<pipeline::LoadedModel>(DefaultAllocator());
    // The LEVEL sits at index 0, the BASE at index 1 - the shape that shifted the slots.
    (void)prepared->model.addMesh(makeMesh(u8"Part_LOD1"));
    (void)prepared->model.addMesh(makeMesh(u8"Part"));
    auto* node = new model::ModelBone();
    node->setName(u8"PartNode");
    node->meshIndex = 1; // references the BASE by model index
    (void)prepared->model.addBone(node);
    prepared->model.calculateBounds();

    pipeline::ModelFileImporter importer;
    Result<foundation::content::Instance*> imported = importer.Import(
        fakeSource.AsView(), pipeline::ImportContext{project->SourcesRoot()},
        *project->SourceDb().RootGroup(), nullptr, prepared.Get(), nullptr);
    REQUIRE(imported.HasValue());

    RefPtr<ISerializable> object = imported.Value()->ReadObject();
    auto* manifestAsset = Cast<pipeline::ModelManifestAsset>(object.Get());
    REQUIRE(manifestAsset != nullptr);
    const foundation::model::ModelManifestSource& manifest = manifestAsset->manifest;

    // BOTH model meshes hold a slot: the folded level as nil, the base as a real asset -
    // and the node's model-space index resolves to the base's guid.
    REQUIRE(manifest.meshGuids.Size() == 2u);
    CHECK(manifest.meshGuids[0].IsNil());
    CHECK(!manifest.meshGuids[1].IsNil());
    REQUIRE(manifest.nodes.Size() == 1u);
    CHECK(manifest.nodes[0].meshIndex == 1);
    foundation::content::Instance* part =
        imported.Value()->OwningGroup().GetInstance(u8"Part");
    REQUIRE(part != nullptr);
    CHECK(part->Id() == manifest.meshGuids[1]);
    CHECK(imported.Value()->OwningGroup().GetInstance(u8"Part_LOD1") == nullptr);
}
