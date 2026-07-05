// Draconic::SceneEditor - the `draconic.scene.editor` module (tooling).
//
// The scene SAVE / cook path: capture a live Scene into a content-DB instance (a
// SceneDocument primary + the serialized world in the "scene" data stream), using the
// same bidirectional SerializeScene the runtime loads with. For scenes the cooked file
// IS the authored form, so "save" is the editor-side operation (the mirror of the
// runtime LoadScene). Never linked by the runtime.

module;
#include "Core/Prelude.h"

export module draconic.scene.editor;

import draconic.core;
import draconic.content;
import draconic.scene;
import draconic.scene.resource;

using namespace draconic::core;

export namespace draconic::scene {

// Captures `scene` into `instance`: writes the SceneDocument primary (name) so the
// instance materializes + is discoverable, then the serialized world into the "scene"
// data stream. Round-trips with LoadScene.
inline Status SaveScene(Scene& scene, draconic::content::Instance& instance) {
    SceneDocument doc;
    doc.name = String(scene.Name());
    const Status wrote = instance.WriteObject(doc);
    if (!wrote.IsOk()) { return wrote; }

    MemoryStream buffer;
    BinarySerializer ser(buffer, SerializeMode::Write);
    SerializeScene(ser, scene);
    return instance.WriteData(u8"scene", buffer.Bytes());
}

} // namespace draconic::scene
