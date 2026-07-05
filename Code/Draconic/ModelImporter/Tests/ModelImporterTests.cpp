// Draconic::ModelImporter tests — load a real glTF, cook it through the importer into a
// content DB, then bind the cooked ModelResource back through the resource manager and
// verify the whole convert -> cook -> bind chain (manifest nodes + resolved meshes).

#include "Core/Prelude.h"
#include <doctest/doctest.h>

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.geometry;
import draconic.geometry.resource;
import draconic.animation;
import draconic.animation.resource;
import draconic.model;
import draconic.modelimporter;

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
    content::ContentDatabase db(mount);

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
    content::ContentDatabase db(mount);

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
