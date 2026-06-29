// Raptor::ModelImporter tests — load a real glTF, cook it through the importer into a
// content DB, then bind the cooked ModelResource back through the resource manager and
// verify the whole convert -> cook -> bind chain (manifest nodes + resolved meshes).

#include "Core/Prelude.h"
#include <doctest/doctest.h>

import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.resource;
import raptor.geometry;
import raptor.geometry.resource;
import raptor.model;
import raptor.modelimporter;

using namespace raptor::core;
namespace vfs = raptor::vfs;
namespace ct  = raptor::content;
namespace res = raptor::resource;
namespace geo = raptor::geometry;
namespace mdl = raptor::model;
namespace mi  = raptor::modelimporter;

#ifndef RAPTOR_MI_TEST_DUCK
#define RAPTOR_MI_TEST_DUCK ""
#endif

TEST_CASE("import glTF -> cooked ModelResource round-trips through the resource system")
{
    const StringView duck(reinterpret_cast<const utf8char*>(RAPTOR_MI_TEST_DUCK));
    if (duck.IsEmpty()) { return; }   // path not configured (skip)

    mi::RegisterModelImporterTypes();   // make the cooked types deserializable

    vfs::NativeFileSystem mount(u8"raptor_modelimporter_test_db");
    ct::ContentDatabase db(mount);

    // Cook the model file into the DB; get back the manifest (ModelResource) Guid.
    Guid modelGuid;
    const mdl::ModelLoadResult r = mi::LoadAndCook(duck, db, u8"Duck", modelGuid);
    REQUIRE(r == mdl::ModelLoadResult::Ok);
    REQUIRE_FALSE(modelGuid.IsNil());

    // Bind the composite model: ModelFactory resolves its meshes via StaticMeshFactory.
    res::ResourceManager manager(db);
    geo::StaticMeshFactory meshFactory;
    mi::ModelFactory       modelFactory;
    manager.AddFactory(&meshFactory);
    manager.AddFactory(&modelFactory);

    res::Proxy<mi::ModelResource> model = manager.Bind<mi::ModelResource>(modelGuid);
    REQUIRE(model);
    CHECK(model->nodes.Size() > 0);
    CHECK(model->meshes.Size() > 0);

    // At least one node references a mesh, and that mesh resolved with real geometry.
    bool sawMesh = false;
    for (const mi::ModelNode& n : model->nodes) {
        if (n.meshIndex >= 0 && static_cast<usize>(n.meshIndex) < model->meshes.Size()) {
            geo::StaticMesh* mesh = model->meshes[static_cast<usize>(n.meshIndex)].Get();
            REQUIRE(mesh != nullptr);
            CHECK(mesh->VertexCount() > 0);
            CHECK(mesh->IndexCount() > 0);
            sawMesh = true;
        }
    }
    CHECK(sawMesh);
}
