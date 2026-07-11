// draconic.editor.scene headless tests: EditorCamera orientation math and the scene asset
// creator (unique naming, SceneDocument primary, project round-trip). The page itself needs a
// running host/renderer and is exercised in the editor app (on-screen path).

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.content;
import draconic.scene;
import draconic.scene.resource;
import draconic.editor.core;
import draconic.editor.scene;

using namespace draconic::core;
using namespace draconic::editor;

namespace
{
    void RemoveProjectTree(StringView root)
    {
        FileDelete(PathJoin(root, u8"Project.xml"));
        FileDelete(PathJoin(root, u8"Content/Scenes/Scene.xasset"));
        FileDelete(PathJoin(root, u8"Content/Scenes/Scene2.xasset"));
        RemoveDirectory(PathJoin(root, u8"Content/Scenes"));
        RemoveDirectory(PathJoin(root, u8"Content"));
        RemoveDirectory(PathJoin(root, u8"Sources"));
        RemoveDirectory(PathJoin(root, u8"Cooked"));
        RemoveDirectory(PathJoin(root, u8"Editor"));
        RemoveDirectory(PathJoin(root, u8".cache"));
        RemoveDirectory(root);
    }
}

TEST_CASE("editor-camera: orientation basis stays orthonormal under yaw/pitch")
{
    EditorCamera cam;
    cam.yaw = 0.7f;
    cam.pitch = -0.4f;

    const Float3 f = cam.Forward();
    const Float3 r = cam.Right();
    const Float3 u = cam.Up();

    CHECK(Dot(f, r) == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(Dot(f, u) == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(Dot(r, u) == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(Dot(f, f) == doctest::Approx(1.0f).epsilon(0.001f));

    // Defaults: looking mostly forward-down (-Z with a downward tilt).
    EditorCamera def;
    CHECK(def.Forward().z < 0.0f);
    CHECK(def.Forward().y < 0.0f);
}

TEST_CASE("editor-scene: CreateSceneInstance makes uniquely-named SceneDocument instances")
{
    GlobalTypeRegistry().Register(draconic::scene::SceneDocument::StaticType());
    RegisterSerializable<draconic::scene::SceneDocument>();

    const StringView dir = u8"draconic_editor_scene_test_project";
    RemoveProjectTree(dir);
    REQUIRE(EditorProject::Create(dir, u8"P").IsOk());
    UniquePtr<EditorProject> project = EditorProject::Open(dir);
    REQUIRE(static_cast<bool>(project));

    EditorContext ctx;
    ctx.SetProject(project.Get());

    // No project -> null.
    EditorContext empty;
    CHECK(CreateSceneInstance(empty) == nullptr);

    draconic::content::Instance* first = CreateSceneInstance(ctx);
    REQUIRE(first != nullptr);
    CHECK(first->Name() == u8"Scene");
    CHECK(first->Path() == u8"Scenes/Scene");
    CHECK(first->TypeName() == u8"SceneDocument");

    // The primary object materialized and round-trips.
    RefPtr<ISerializable> obj = first->ReadObject();
    REQUIRE(obj.Get() != nullptr);
    auto* doc = Cast<draconic::scene::SceneDocument>(obj.Get());
    REQUIRE(doc != nullptr);
    CHECK(doc->name == u8"Scene");

    // Second create picks a unique name in the same group.
    draconic::content::Instance* second = CreateSceneInstance(ctx);
    REQUIRE(second != nullptr);
    CHECK(second->Name() == u8"Scene2");
    CHECK(second->Id() != first->Id());

    RemoveProjectTree(dir);
}
