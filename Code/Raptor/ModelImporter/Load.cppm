/// Raptor::ModelImporter:load — load a model file + cook it in one call.
///
/// Convenience over the model loaders + the cook step: registers the glTF/FBX
/// loaders, loads `path` into a Model IR, then cooks it into `db`, yielding the
/// ImportedModel manifest. This is the clean seam an app (or, later, the editor)
/// drives; it keeps the loader dependency inside the importer library.

module;
#include "Core/Prelude.h"

export module raptor.modelimporter:load;

import raptor.core;
import raptor.model;
import raptor.model.io;
import raptor.model.gltf;
import raptor.model.fbx;
import raptor.content;
import :cook;

using namespace raptor::core;
namespace mdl = raptor::model;
namespace ct  = raptor::content;

export namespace raptor::modelimporter {

// Load a glTF/GLB/FBX/OBJ file and cook it into `db`. `prefix` namespaces the created
// content instances. Returns the load result (Ok on success); on a load failure the
// cook is skipped. On Ok, `outModelGuid` is the cooked manifest (ModelResource) Guid to
// Bind at runtime — it pulls in the model's meshes via dependency edges.
[[nodiscard]] inline mdl::ModelLoadResult LoadAndCook(StringView path, ct::ContentDatabase& db,
                                                      StringView prefix, Guid& outModelGuid)
{
    mdl::gltf::GltfLoader gltf;
    mdl::fbx::FbxLoader   fbx;
    mdl::io::registerLoader(&gltf);
    mdl::io::registerLoader(&fbx);

    mdl::Model model;
    const mdl::ModelLoadResult r = mdl::io::loadModel(path, model);

    mdl::io::unregisterLoader(&fbx);
    mdl::io::unregisterLoader(&gltf);

    if (r != mdl::ModelLoadResult::Ok) { return r; }

    const Status s = CookModel(model, db, prefix, outModelGuid);
    return s.IsOk() ? mdl::ModelLoadResult::Ok : mdl::ModelLoadResult::InvalidData;
}

} // namespace raptor::modelimporter
