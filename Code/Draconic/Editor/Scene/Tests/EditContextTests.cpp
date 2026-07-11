// SceneEditContext tests (headless live Scene): every mutation is an undoable command by Guid.
// Covers create (redo keeps the SAME Guid), rename (merge), reparent (cycle-refusal, undo),
// destroy (undo restores the FULL subtree - names, transforms, hierarchy, active flags, and
// serializable components - the fix for Sedulous's lossy destroy-undo), and selection pruning.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.scene;
import draconic.editor.core;
import draconic.editor.scene;

using namespace draconic::core;
using namespace draconic::editor;
namespace dscene = draconic::scene;

namespace
{
    struct HealthComponent
    {
        i32 amount = 0;
    };

    void Serialize(ISerializer& ar, HealthComponent& c)
    {
        draconic::core::Serialize(ar, "amount", c.amount);
    }

    class HealthManager final : public dscene::SerializableComponentManager<HealthComponent>
    {
    public:
        HealthManager() : SerializableComponentManager(u8"test.health") {}
    };
}

TEST_CASE("scene-edit: create entity - undo/redo keeps the same Guid, parents apply")
{
    dscene::Scene scene(u8"t");
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    const Guid parent = edit.CreateEntity(u8"Parent");
    REQUIRE(parent != Guid{});
    CHECK(scene.EntityCount() == 1);
    CHECK(edit.EntitySelection().Contains(parent));   // created entity becomes the selection

    const Guid child = edit.CreateEntity(u8"Child", parent);
    REQUIRE(child != Guid{});
    CHECK(scene.GetParent(edit.Resolve(child)) == edit.Resolve(parent));

    commands.Undo();   // child gone
    CHECK(scene.EntityCount() == 1);
    CHECK(!edit.Resolve(child).IsAssigned());

    commands.Redo();   // child back with the SAME Guid + parent
    CHECK(scene.EntityCount() == 2);
    REQUIRE(edit.Resolve(child).IsAssigned());
    CHECK(scene.GetParent(edit.Resolve(child)) == edit.Resolve(parent));
}

TEST_CASE("scene-edit: rename merges and undoes to the original")
{
    dscene::Scene scene;
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    const Guid id = edit.CreateEntity(u8"Original");
    edit.RenameEntity(id, u8"First");
    edit.RenameEntity(id, u8"Second");   // merges into the previous rename
    CHECK(scene.GetEntityName(edit.Resolve(id)) == u8"Second");

    commands.Undo();   // ONE undo reverts both renames
    CHECK(scene.GetEntityName(edit.Resolve(id)) == u8"Original");
    commands.Redo();
    CHECK(scene.GetEntityName(edit.Resolve(id)) == u8"Second");
}

TEST_CASE("scene-edit: reparent - undo restores, cycles and no-ops are refused")
{
    dscene::Scene scene;
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    const Guid a = edit.CreateEntity(u8"A");
    const Guid b = edit.CreateEntity(u8"B", a);
    const Guid c = edit.CreateEntity(u8"C");
    const usize baseline = commands.Size();

    edit.ReparentEntity(b, c);   // A/B -> C/B
    CHECK(scene.GetParent(edit.Resolve(b)) == edit.Resolve(c));

    commands.Undo();
    CHECK(scene.GetParent(edit.Resolve(b)) == edit.Resolve(a));

    // A cycle (parent A under its own subtree via B... i.e. A under B) is refused + dropped.
    commands.Redo();   // back to C/B
    edit.ReparentEntity(a, b);   // a's descendant?? b is not under a anymore, so this is LEGAL
    CHECK(scene.GetParent(edit.Resolve(a)) == edit.Resolve(b));
    edit.ReparentEntity(c, a);   // c is now an ancestor of a?? a is under b under c: cycle -> refused
    CHECK(scene.GetParent(edit.Resolve(c)) == dscene::EntityHandle::Invalid());

    // Self-parenting refused; no-op reparent (same parent) refused.
    edit.ReparentEntity(b, b);
    CHECK(scene.GetParent(edit.Resolve(b)) == edit.Resolve(c));
    const usize before = commands.Size();
    edit.ReparentEntity(b, c);   // already C's child - dropped, no undo pollution
    CHECK(commands.Size() == before);
    (void)baseline;
}

TEST_CASE("scene-edit: destroy undo restores the full subtree with components")
{
    dscene::Scene scene;
    auto* health = scene.AddSystem<HealthManager>();
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    const Guid root = edit.CreateEntity(u8"Root");
    const Guid childA = edit.CreateEntity(u8"ChildA", root);
    const Guid grand = edit.CreateEntity(u8"Grandchild", childA);
    const Guid childB = edit.CreateEntity(u8"ChildB", root);

    // Non-default state that must survive the round-trip.
    Transform t;
    t.position = Float3{ 1.0f, 2.0f, 3.0f };
    scene.SetLocalTransform(edit.Resolve(childA), t);
    scene.SetActive(edit.Resolve(childB), false);
    health->Add(edit.Resolve(grand)).amount = 77;
    health->Add(edit.Resolve(root)).amount = 5;

    edit.EntitySelection().Set(grand);
    edit.DestroyEntity(root);

    CHECK(scene.EntityCount() == 0);
    CHECK(health->ComponentCount() == 0);
    CHECK(edit.EntitySelection().IsEmpty());   // doomed subtree deselected

    commands.Undo();

    CHECK(scene.EntityCount() == 4);
    REQUIRE(edit.Resolve(root).IsAssigned());
    REQUIRE(edit.Resolve(childA).IsAssigned());
    REQUIRE(edit.Resolve(grand).IsAssigned());
    REQUIRE(edit.Resolve(childB).IsAssigned());

    // Hierarchy restored.
    CHECK(scene.GetParent(edit.Resolve(childA)) == edit.Resolve(root));
    CHECK(scene.GetParent(edit.Resolve(grand)) == edit.Resolve(childA));
    CHECK(scene.GetParent(edit.Resolve(childB)) == edit.Resolve(root));

    // Names, transform, active flag restored.
    CHECK(scene.GetEntityName(edit.Resolve(grand)) == u8"Grandchild");
    CHECK(scene.GetLocalTransform(edit.Resolve(childA)).position.y == doctest::Approx(2.0f));
    CHECK(!scene.IsActive(edit.Resolve(childB)));

    // Components restored with their data.
    REQUIRE(health->HasComponent(edit.Resolve(grand)));
    CHECK(health->Get(edit.Resolve(grand))->amount == 77);
    REQUIRE(health->HasComponent(edit.Resolve(root)));
    CHECK(health->Get(edit.Resolve(root))->amount == 5);

    // Redo destroys it all again.
    commands.Redo();
    CHECK(scene.EntityCount() == 0);
    CHECK(health->ComponentCount() == 0);
}

TEST_CASE("scene-edit: destroying a missing entity is a safe no-op")
{
    dscene::Scene scene;
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    edit.DestroyEntity(Guid{ 1, 2 });
    CHECK(commands.Size() == 0);
    edit.RenameEntity(Guid{ 1, 2 }, u8"nope");   // failed execute -> dropped
    CHECK(commands.Size() == 0);
}
