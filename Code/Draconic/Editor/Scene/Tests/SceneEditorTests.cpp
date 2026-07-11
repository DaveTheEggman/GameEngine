// draconic.editor.scene headless tests: EditorCamera orientation math and the scene asset
// creator (unique naming, SceneDocument primary, project round-trip). The page itself needs a
// running host/renderer and is exercised in the editor app (on-screen path).

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.content;
import draconic.scene;
import draconic.scene.resource;
import draconic.ui;
import draconic.ui.toolkit;
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

TEST_CASE("editor-camera: LookAt aims forward at the target with a level horizon")
{
    EditorCamera cam;
    cam.position = Float3{ 6.0f, 5.0f, 10.0f };
    cam.LookAt(Float3{ 0.0f, 0.0f, 0.0f });

    // Forward matches the normalized direction to the target.
    const Float3 toTarget = Normalized(Float3{ -6.0f, -5.0f, -10.0f });
    const Float3 f = cam.Forward();
    CHECK(f.x == doctest::Approx(toTarget.x).epsilon(0.001f));
    CHECK(f.y == doctest::Approx(toTarget.y).epsilon(0.001f));
    CHECK(f.z == doctest::Approx(toTarget.z).epsilon(0.001f));

    // No roll: the right vector stays in the ground plane (level horizon).
    CHECK(cam.Right().y == doctest::Approx(0.0f).epsilon(0.001f));

    // Orbit pivot moved to the target.
    CHECK(cam.focusDistance == doctest::Approx(12.688f).epsilon(0.001f));

    // The struct defaults agree with LookAt(origin) from the default position.
    EditorCamera def;
    CHECK(def.yaw == doctest::Approx(cam.yaw).epsilon(0.02f));
    CHECK(def.pitch == doctest::Approx(cam.pitch).epsilon(0.02f));

    // Degenerate target (== position) is a safe no-op.
    EditorCamera still;
    still.position = Float3{ 1.0f, 2.0f, 3.0f };
    const f32 yawBefore = still.yaw;
    still.LookAt(Float3{ 1.0f, 2.0f, 3.0f });
    CHECK(still.yaw == yawBefore);
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

TEST_CASE("hierarchy: collapse state survives snapshot rebuilds")
{
    dscene::Scene scene;
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);
    SceneHierarchyView hierarchy(edit);

    const Guid parent = edit.CreateEntity(u8"Parent");
    (void)edit.CreateEntity(u8"Child", parent);
    (void)edit.CreateEntity(u8"Sibling");
    hierarchy.Refresh();

    auto* flat = hierarchy.Tree()->InternalTreeView()->FlatAdapter();
    REQUIRE(flat != nullptr);
    CHECK(flat->ItemCount() == 3);   // Parent (expanded) + Child + Sibling

    // Collapse Parent (pre-order nodeId 0), then force a rebuild by editing the scene.
    flat->Collapse(0);
    CHECK(flat->ItemCount() == 2);
    (void)edit.CreateEntity(u8"Another");
    hierarchy.Refresh();

    // SetAdapter recreated the flat view; Parent must STAY collapsed, new root visible.
    flat = hierarchy.Tree()->InternalTreeView()->FlatAdapter();
    CHECK(flat->ItemCount() == 3);   // Parent (collapsed) + Sibling + Another
    CHECK_FALSE(flat->IsExpanded(0));

    // Re-expanding sticks across the next rebuild too.
    flat->Expand(0);
    (void)edit.CreateEntity(u8"YetAnother");
    hierarchy.Refresh();
    flat = hierarchy.Tree()->InternalTreeView()->FlatAdapter();
    CHECK(flat->ItemCount() == 5);
    CHECK(flat->IsExpanded(0));
}

// Repro: switching the selected entity must rebuild the inspector for the NEW entity.
TEST_CASE("inspector: rebuilds when the selection switches entities")
{
    dscene::Scene scene;
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);
    SceneInspectorView inspector(edit);

    const Guid a = edit.CreateEntity(u8"Alpha");
    const Guid b = edit.CreateEntity(u8"Beta");

    edit.EntitySelection().Set(a);
    inspector.Refresh();
    auto* nameEditor = draconic::core::Cast<draconic::ui::toolkit::StringEditor>(
        inspector.Grid()->GetProperty(u8"Name"));
    REQUIRE(nameEditor != nullptr);
    CHECK(nameEditor->Value() == u8"Alpha");

    edit.EntitySelection().Set(b);
    inspector.Refresh();
    nameEditor = draconic::core::Cast<draconic::ui::toolkit::StringEditor>(
        inspector.Grid()->GetProperty(u8"Name"));
    REQUIRE(nameEditor != nullptr);
    CHECK(nameEditor->Value() == u8"Beta");

    // Clearing the selection empties the grid.
    edit.EntitySelection().Clear();
    inspector.Refresh();
    CHECK(inspector.Grid()->PropertyCount() == 0);
}
