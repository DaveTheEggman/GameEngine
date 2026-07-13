// SceneEditContext tests (headless live Scene): every mutation is an undoable command by Guid.
// Covers create (redo keeps the SAME Guid), rename (merge), reparent (cycle-refusal, undo),
// destroy (undo restores the FULL subtree - names, transforms, hierarchy, active flags, and
// serializable components - the fix for Sedulous's lossy destroy-undo), and selection pruning.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.scene;
import draconic.render.subsystem;
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

TEST_CASE("scene-edit: sibling reorder command with undo/redo")
{
    dscene::Scene scene;
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    const Guid a = edit.CreateEntity(u8"A");
    const Guid b = edit.CreateEntity(u8"B");
    const Guid c = edit.CreateEntity(u8"C");

    // Move C before A: order c, a, b.
    edit.MoveEntityBefore(c, a);
    CHECK(scene.GetFirstRoot() == edit.Resolve(c));

    // Undo restores the exact old position (C was last).
    commands.Undo();
    CHECK(scene.GetFirstRoot() == edit.Resolve(a));
    CHECK(scene.GetNextSibling(edit.Resolve(b)) == edit.Resolve(c));
    commands.Redo();
    CHECK(scene.GetFirstRoot() == edit.Resolve(c));

    // Move A to the END of the root list (empty sibling): c, b, a.
    edit.MoveEntityBefore(a, Guid{});
    CHECK(scene.GetNextSibling(edit.Resolve(b)) == edit.Resolve(a));
    commands.Undo();   // back to c, a, b (A was before B)
    CHECK(scene.GetNextSibling(edit.Resolve(c)) == edit.Resolve(a));
    CHECK(scene.GetNextSibling(edit.Resolve(a)) == edit.Resolve(b));

    // No-op move (already before B) is dropped, not pushed.
    const usize size = commands.Size();
    edit.MoveEntityBefore(a, b);
    CHECK(commands.Size() == size);

    // Cycle refused: moving A before a slot under its own subtree.
    edit.ReparentEntity(b, a);   // b under a
    const Guid d = edit.CreateEntity(u8"D", b);
    const usize size2 = commands.Size();
    edit.MoveEntityBefore(a, d);   // slot parent = b, inside a's subtree
    CHECK(commands.Size() == size2);
}

TEST_CASE("scene-edit: reparent preserves the world transform; undo restores the exact local")
{
    dscene::Scene scene;
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    const Guid parent = edit.CreateEntity(u8"Parent");
    const Guid child = edit.CreateEntity(u8"Child");

    Transform tp;
    tp.position = Float3{ 5.0f, 0.0f, 0.0f };
    tp.rotation = Quaternion::FromAxisAngle(Float3{ 0, 1, 0 }, 0.5f);
    scene.SetLocalTransform(edit.Resolve(parent), tp);
    Transform tc;
    tc.position = Float3{ 1.0f, 2.0f, 3.0f };
    scene.SetLocalTransform(edit.Resolve(child), tc);

    const Float4x4 worldBefore = scene.ComposeWorldMatrix(edit.Resolve(child));

    edit.ReparentEntity(child, parent);
    const Float4x4 worldAfter = scene.ComposeWorldMatrix(edit.Resolve(child));
    for (i32 r = 0; r < 4; ++r)
        for (i32 c = 0; c < 4; ++c)
            CHECK(worldAfter.m[r][c] == doctest::Approx(worldBefore.m[r][c]).epsilon(0.001f));

    // Undo: exact original local (bit-identical restore, not a decompose round-trip).
    commands.Undo();
    CHECK(scene.GetLocalTransform(edit.Resolve(child)).position.x == 1.0f);
    CHECK(scene.GetLocalTransform(edit.Resolve(child)).position.y == 2.0f);
    CHECK(scene.GetParent(edit.Resolve(child)) == dscene::EntityHandle::Invalid());

    // Redo reproduces the preserved world again.
    commands.Redo();
    const Float4x4 worldRedo = scene.ComposeWorldMatrix(edit.Resolve(child));
    for (i32 r = 0; r < 4; ++r)
        for (i32 c = 0; c < 4; ++c)
            CHECK(worldRedo.m[r][c] == doctest::Approx(worldBefore.m[r][c]).epsilon(0.001f));

    // Same-parent reorder keeps the local EXACT (no decompose noise).
    const Guid s1 = edit.CreateEntity(u8"S1", parent);
    Transform ts;
    ts.position = Float3{ 0.25f, 0.5f, 0.75f };
    scene.SetLocalTransform(edit.Resolve(s1), ts);
    edit.MoveEntityBefore(s1, child);   // reorder within `parent`
    CHECK(scene.GetLocalTransform(edit.Resolve(s1)).position.x == 0.25f);
    CHECK(scene.GetLocalTransform(edit.Resolve(s1)).position.z == 0.75f);
}

namespace
{
    // A reflected (but NOT serializable) component - exercises the property paths and the
    // reflected-snapshot remove-undo.
    enum class TestMode : u32 { Off = 0, Slow = 1, Fast = 2 };

    struct WidgetComponent
    {
        f32 speed = 1.0f;
        bool spin = false;
        Float3 offset{ 0, 0, 0 };
        TestMode mode = TestMode::Off;
    };

    class WidgetManager final : public dscene::ComponentManager<WidgetComponent>
    {
    };
}

DRACONIC_REFLECT_ENUM(TestMode, "draconic::editor::test")
{
    builder.Value("Off", TestMode::Off);
    builder.Value("Slow", TestMode::Slow);
    builder.Value("Fast", TestMode::Fast);
}

DRACONIC_REFLECT_VALUE(WidgetComponent, "draconic::editor::test")
{
    builder.Property<&WidgetComponent::speed>("speed")
           .Property<&WidgetComponent::spin>("spin")
           .Property<&WidgetComponent::offset>("offset")
           .Property<&WidgetComponent::mode>("mode");
}

TEST_CASE("scene-edit: transform + active commands (merge, undo)")
{
    dscene::Scene scene;
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);
    const Guid id = edit.CreateEntity(u8"E");
    const usize baseline = commands.Size();

    // A "drag": many transform sets merge into ONE undo entry.
    Transform t;
    for (i32 i = 1; i <= 5; ++i)
    {
        t.position = Float3{ static_cast<f32>(i), 0, 0 };
        edit.SetLocalTransform(id, t);
    }
    CHECK(commands.Size() == baseline + 1);
    CHECK(scene.GetLocalTransform(edit.Resolve(id)).position.x == doctest::Approx(5.0f));
    commands.Undo();
    CHECK(scene.GetLocalTransform(edit.Resolve(id)).position.x == doctest::Approx(0.0f));

    edit.SetEntityActive(id, false);
    CHECK(!scene.IsActive(edit.Resolve(id)));
    commands.Undo();
    CHECK(scene.IsActive(edit.Resolve(id)));
    edit.SetEntityActive(id, true);   // no-op - dropped
    CHECK(commands.Size() == baseline + 1);
}

TEST_CASE("scene-edit: component property commands (variant + raw enum, merge, undo)")
{
    DraconicRegisterEnum_TestMode();
    DraconicRegisterValue_WidgetComponent();

    dscene::Scene scene;
    auto* widgets = scene.AddSystem<WidgetManager>();
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);
    const Guid id = edit.CreateEntity(u8"E");
    const TypeInfo* type = widgets->ComponentType();

    // Add via command; undo removes; redo re-adds.
    edit.AddComponent(id, type);
    CHECK(widgets->HasComponent(edit.Resolve(id)));
    commands.Undo();
    CHECK(!widgets->HasComponent(edit.Resolve(id)));
    commands.Redo();
    REQUIRE(widgets->HasComponent(edit.Resolve(id)));
    edit.AddComponent(id, type);   // already present - dropped
    const usize afterAdd = commands.Size();

    // Variant path with merge: a scrub of `speed` is one undo entry.
    edit.SetComponentProperty(id, type, "speed", Variant::From<f32>(2.0f));
    edit.SetComponentProperty(id, type, "speed", Variant::From<f32>(3.5f));
    CHECK(commands.Size() == afterAdd + 1);
    CHECK(widgets->Get(edit.Resolve(id))->speed == doctest::Approx(3.5f));
    commands.Undo();
    CHECK(widgets->Get(edit.Resolve(id))->speed == doctest::Approx(1.0f));
    commands.Redo();

    // Different property does NOT merge.
    edit.SetComponentProperty(id, type, "offset", Variant::From<Float3>(Float3{ 1, 2, 3 }));
    CHECK(commands.Size() == afterAdd + 2);
    CHECK(widgets->Get(edit.Resolve(id))->offset.y == doctest::Approx(2.0f));

    // Raw (enum) path through PropertyInfo::address.
    edit.SetComponentPropertyRaw(id, type, "mode", static_cast<i64>(TestMode::Fast));
    CHECK(widgets->Get(edit.Resolve(id))->mode == TestMode::Fast);
    commands.Undo();
    CHECK(widgets->Get(edit.Resolve(id))->mode == TestMode::Off);

    // Remove undo restores the reflected state (non-serializable manager).
    widgets->Get(edit.Resolve(id))->spin = true;
    edit.RemoveComponent(id, type);
    CHECK(!widgets->HasComponent(edit.Resolve(id)));
    commands.Undo();
    REQUIRE(widgets->HasComponent(edit.Resolve(id)));
    CHECK(widgets->Get(edit.Resolve(id))->speed == doctest::Approx(3.5f));
    CHECK(widgets->Get(edit.Resolve(id))->spin);
    CHECK(widgets->Get(edit.Resolve(id))->offset.z == doctest::Approx(3.0f));
}

TEST_CASE("scene-edit: remove-component undo via serialization blob (serializable manager)")
{
    dscene::Scene scene;
    auto* health = scene.AddSystem<HealthManager>();
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);
    const Guid id = edit.CreateEntity(u8"E");

    health->Add(edit.Resolve(id)).amount = 42;
    edit.RemoveComponent(id, health->ComponentType());
    CHECK(!health->HasComponent(edit.Resolve(id)));
    commands.Undo();
    REQUIRE(health->HasComponent(edit.Resolve(id)));
    CHECK(health->Get(edit.Resolve(id))->amount == 42);   // full fidelity via the blob
}

// Regression (user-reported): undoing an entity destroy restored the entity but LOST its
// LightComponent - destroy-undo snapshots components through SERIALIZABLE managers only, and
// the light/camera/probe managers weren't serializable (which also silently dropped them from
// scene saves).
TEST_CASE("edit-context: destroy-undo restores light components (and their values)")
{
    dscene::Scene scene;
    scene.AddSystem<draconic::render::LightComponentManager>();
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    const Guid id = edit.CreateEntity(u8"Sun");
    auto* lights = scene.GetSystem<draconic::render::LightComponentManager>();
    REQUIRE(lights != nullptr);
    {
        draconic::render::LightComponent& light = lights->Add(edit.Resolve(id));
        light.type = draconic::render::LightType::Spot;
        light.intensity = 3.5f;
        light.range = 42.0f;
        light.castsShadows = true;
    }

    edit.DestroyEntity(id);
    CHECK_FALSE(edit.Resolve(id).IsAssigned());

    commands.Undo();
    const dscene::EntityHandle restored = edit.Resolve(id);
    REQUIRE(restored.IsAssigned());
    draconic::render::LightComponent* light = lights->Get(restored);
    REQUIRE(light != nullptr);   // the component came back...
    CHECK(light->type == draconic::render::LightType::Spot);   // ...with its exact values
    CHECK(light->intensity == doctest::Approx(3.5f));
    CHECK(light->range == doctest::Approx(42.0f));
    CHECK(light->castsShadows);
}

TEST_CASE("edit-context: duplicate entity - fresh guids, subtree + components, one undo")
{
    dscene::Scene scene;
    scene.AddSystem<draconic::render::LightComponentManager>();
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);

    const Guid parent = edit.CreateEntity(u8"Rig");
    const Guid child  = edit.CreateEntity(u8"Lamp", parent);
    scene.SetLocalPosition(edit.Resolve(parent), Float3{ 3, 0, 0 });
    auto* lights = scene.GetSystem<draconic::render::LightComponentManager>();
    {
        draconic::render::LightComponent& light = lights->Add(edit.Resolve(child));
        light.intensity = 7.0f;
    }

    const Guid copy = edit.DuplicateEntity(parent);
    REQUIRE(copy != Guid{});
    CHECK(copy != parent);                                       // fresh identity
    const dscene::EntityHandle copyRoot = edit.Resolve(copy);
    REQUIRE(copyRoot.IsAssigned());
    CHECK(scene.GetEntityName(copyRoot) == u8"Rig (2)");   // copies are distinguishable
    CHECK(scene.GetLocalTransform(copyRoot).position.x == doctest::Approx(3.0f));
    CHECK(!scene.GetParent(copyRoot).IsAssigned());              // sibling of the original (root)
    REQUIRE(edit.EntitySelection().Primary() != nullptr);
    CHECK(*edit.EntitySelection().Primary() == copy);            // the copy becomes the selection

    // The child came along, with its component values, under the COPY (not the original).
    REQUIRE(scene.GetChildCount(copyRoot) == 1u);
    const dscene::EntityHandle copyChild = scene.GetFirstChild(copyRoot);
    CHECK(scene.GetEntityName(copyChild) == u8"Lamp");
    CHECK(scene.GetEntityId(copyChild) != child);
    draconic::render::LightComponent* light = lights->Get(copyChild);
    REQUIRE(light != nullptr);
    CHECK(light->intensity == doctest::Approx(7.0f));

    // One undo removes the whole copy; redo brings it back with the SAME fresh guids.
    commands.Undo();
    CHECK_FALSE(edit.Resolve(copy).IsAssigned());
    CHECK(edit.Resolve(parent).IsAssigned());                    // original untouched
    commands.Redo();
    REQUIRE(edit.Resolve(copy).IsAssigned());
    CHECK(lights->Get(scene.GetFirstChild(edit.Resolve(copy))) != nullptr);
}

TEST_CASE("edit-context: copy/paste entities across scenes with fresh guids")
{
    dscene::Scene sceneA;
    sceneA.AddSystem<draconic::render::LightComponentManager>();
    EditorCommandStack commandsA;
    SceneEditContext editA(sceneA, commandsA);

    const Guid src = editA.CreateEntity(u8"Prop");
    const Guid srcChild = editA.CreateEntity(u8"Bulb", src);
    {
        auto* lights = sceneA.GetSystem<draconic::render::LightComponentManager>();
        lights->Add(editA.Resolve(srcChild)).range = 12.0f;
    }
    const Array<byte> blob = editA.CopyEntity(src);
    REQUIRE(!blob.IsEmpty());

    // Paste into a DIFFERENT scene (its own command stack), under a chosen parent.
    dscene::Scene sceneB;
    sceneB.AddSystem<draconic::render::LightComponentManager>();
    EditorCommandStack commandsB;
    SceneEditContext editB(sceneB, commandsB);
    const Guid target = editB.CreateEntity(u8"Holder");

    const Guid pasted = editB.PasteEntities(Span<const byte>{ blob.Data(), blob.Size() }, target);
    REQUIRE(pasted != Guid{});
    const dscene::EntityHandle root = editB.Resolve(pasted);
    REQUIRE(root.IsAssigned());
    CHECK(sceneB.GetEntityName(root) == u8"Prop");
    CHECK(sceneB.GetEntityId(sceneB.GetParent(root)) == target);
    REQUIRE(sceneB.GetChildCount(root) == 1u);
    auto* lightsB = sceneB.GetSystem<draconic::render::LightComponentManager>();
    draconic::render::LightComponent* light = lightsB->Get(sceneB.GetFirstChild(root));
    REQUIRE(light != nullptr);
    CHECK(light->range == doctest::Approx(12.0f));

    // The same blob pastes AGAIN (fresh guids every time); the source scene never changed.
    const Guid pasted2 = editB.PasteEntities(Span<const byte>{ blob.Data(), blob.Size() });
    REQUIRE(pasted2 != Guid{});
    CHECK(pasted2 != pasted);
    CHECK(editA.Resolve(src).IsAssigned());

    // Undo in B removes only the last paste.
    commandsB.Undo();
    CHECK_FALSE(editB.Resolve(pasted2).IsAssigned());
    CHECK(editB.Resolve(pasted).IsAssigned());
}

TEST_CASE("edit-context: copy/paste component - add, overwrite, and exact undo")
{
    dscene::Scene scene;
    scene.AddSystem<draconic::render::LightComponentManager>();
    EditorCommandStack commands;
    SceneEditContext edit(scene, commands);
    auto* lights = scene.GetSystem<draconic::render::LightComponentManager>();

    const Guid a = edit.CreateEntity(u8"A");
    const Guid b = edit.CreateEntity(u8"B");
    lights->Add(edit.Resolve(a)).intensity = 9.0f;

    const Array<byte> blob =
        edit.CopyComponent(a, &TypeOf<draconic::render::LightComponent>());
    REQUIRE(!blob.IsEmpty());
    CHECK(SceneEditContext::PeekComponentTypeId(
              Span<const byte>{ blob.Data(), blob.Size() }) == u8"light");

    // Paste onto an entity WITHOUT the component: adds it. Undo removes it again.
    REQUIRE(edit.PasteComponent(b, Span<const byte>{ blob.Data(), blob.Size() }));
    REQUIRE(lights->Get(edit.Resolve(b)) != nullptr);
    CHECK(lights->Get(edit.Resolve(b))->intensity == doctest::Approx(9.0f));
    commands.Undo();
    CHECK(lights->Get(edit.Resolve(b)) == nullptr);
    commands.Redo();
    REQUIRE(lights->Get(edit.Resolve(b)) != nullptr);

    // Paste onto an entity WITH the component: overwrites; undo restores the prior values.
    lights->Get(edit.Resolve(b))->intensity = 1.0f;
    REQUIRE(edit.PasteComponent(b, Span<const byte>{ blob.Data(), blob.Size() }));
    CHECK(lights->Get(edit.Resolve(b))->intensity == doctest::Approx(9.0f));
    commands.Undo();
    REQUIRE(lights->Get(edit.Resolve(b)) != nullptr);
    CHECK(lights->Get(edit.Resolve(b))->intensity == doctest::Approx(1.0f));
}
