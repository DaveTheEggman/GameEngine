// draconic.script.subsystem tests - the behaviors core, HEADLESS (real Wren VM, real
// Scene, zero device deps): lifecycle dispatch (deferred start, onUpdate(dt), enable/
// disable edges, onDestroy on entity destroy AND scene stop), defaults + hash-keyed
// overrides, the fault-disables-one-behavior rule, hot reload (product swap ->
// re-instantiate -> overrides re-applied, transient state reset), the entity facade
// (+ Log/Time/Random), component wire symmetry through SerializeScene, and the
// prefab-override round-trip (zero new code - the component payload carries it).

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <initializer_list>

import draconic.core;
import draconic.scene;
import draconic.scene.resource;
import draconic.resource;
import draconic.script;
import draconic.script.wren;
import draconic.script.resource;
import draconic.script.subsystem;

using namespace draconic::core;
using namespace draconic::script;
namespace dscene = draconic::scene;

namespace
{
    // A cooked-product stand-in: samples/tests build ScriptClass products directly
    // (the factory test proves the cooked path; here the RUNTIME is under test).
    [[nodiscard]] RefPtr<ScriptClass> MakeClass(
        StringView className, StringView source,
        std::initializer_list<StringView> handlers,
        std::initializer_list<ScriptPropertyDesc> properties = {})
    {
        RefPtr<ScriptClass> cls = MakeRef<ScriptClass>(DefaultAllocator());
        cls->language = String(u8"wren");
        cls->className = String(className);
        cls->source = String(source);
        for (StringView handler : handlers) { cls->handlers.PushBack(String(handler)); }
        for (const ScriptPropertyDesc& property : properties)
        {
            cls->properties.PushBack(property);
        }
        cls->BuildProfileName();
        return cls;
    }

    [[nodiscard]] ScriptPropertyDesc FloatProperty(StringView name, f64 defaultValue)
    {
        ScriptPropertyDesc desc;
        desc.name = String(name);
        desc.hash = ScriptPropertyNameHash(name);
        desc.type = ScriptPropertyType::Float;
        desc.defaultValue.kind = ScriptPropertyType::Float;
        desc.defaultValue.number = defaultValue;
        return desc;
    }

    [[nodiscard]] ScriptPropertyDesc EntityProperty(StringView name)
    {
        ScriptPropertyDesc desc;
        desc.name = String(name);
        desc.hash = ScriptPropertyNameHash(name);
        desc.type = ScriptPropertyType::Entity;
        desc.defaultValue.kind = ScriptPropertyType::Entity;
        return desc;
    }

    struct ScriptedScene
    {
        dscene::Scene scene{ u8"script-test" };
        ScriptRunHost host;
        ScriptComponentManager* components = nullptr;
        ScriptSceneSystem* scripts = nullptr;

        ScriptedScene()
        {
            // Script-error logs reach the test output (silent otherwise).
            static bool logReady = []() {
                static ConsoleSink sink;
                GlobalLogger().AddSink(&sink);
                return true;
            }();
            (void)logReady;
            draconic::script::wren::RegisterWrenScriptBackend();
            RegisterCoreTypes();
            RegisterScriptComponentReflection();
            RegisterScriptFacadeReflection();
            components = scene.AddSystem<ScriptComponentManager>();
            scripts = scene.AddSystem<ScriptSceneSystem>();
            components->SetScriptSystem(scripts);
            scripts->SetRunHost(&host);
            // Wire the single-scene message route (the ScriptSubsystem installs a
            // scene-multiplexer in a real run; here one system owns every entity).
            ScriptSceneSystem* system = scripts;
            host.Binding().dispatchMessage =
                Function<void(dscene::Scene*, dscene::EntityHandle, StringView,
                              Span<const Variant>)>{
                    [system](dscene::Scene*, dscene::EntityHandle target, StringView message,
                             Span<const Variant> args) {
                        system->EnqueueMessage(target, message, args);
                    } };
        }

        dscene::EntityHandle AddScripted(const RefPtr<ScriptClass>& cls, StringView name)
        {
            dscene::EntityHandle e = scene.CreateEntity(name);
            ScriptComponent& c = components->Add(e);
            ScriptBehavior behavior;
            behavior.script = cls;
            c.behaviors.PushBack(Move(behavior));
            return e;
        }

        void Start()
        {
            scene.Start();
            scene.SetSimulationEnabled(true);
        }

        void Frame(f32 deltaTime = 0.5f)
        {
            host.Binding().timeSeconds += static_cast<f64>(deltaTime);
            host.Binding().deltaSeconds = deltaTime;
            scene.Update(deltaTime);
        }
    };

    bool Near(f32 a, f32 b) { return Abs(a - b) < 1e-4f; }
}

TEST_CASE("script.scene: lifecycle - onStart once (deferred to the first simulated "
          "tick), onUpdate(dt) with real dt, gated to simulation")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> mover = MakeClass(u8"Mover",
        u8"class Mover {\n"
        u8"    construct new(entity) {\n"
        u8"        _entity = entity\n"
        u8"        _speed = 2.0\n"
        u8"        _entity.setName(\"constructed\")\n"
        u8"    }\n"
        u8"    speed=(v) { _speed = v }\n"
        u8"    onStart() { _entity.setName(\"started\") }\n"
        u8"    onUpdate(dt) {\n"
        u8"        var p = _entity.position()\n"
        u8"        _entity.setPosition(p.x + _speed * dt, p.y, p.z)\n"
        u8"    }\n"
        u8"}\n",
        { u8"onStart", u8"onUpdate" }, { FloatProperty(u8"speed", 2.0) });

    const dscene::EntityHandle e = bed.AddScripted(mover, u8"walker");

    // Not simulating yet: nothing instantiates, nothing runs.
    bed.scene.Start();
    bed.scene.SetSimulationEnabled(false);
    bed.Frame();
    CHECK(bed.scene.GetEntityName(e) == StringView(u8"walker"));
    CHECK_FALSE(bed.host.IsActive());

    bed.scene.SetSimulationEnabled(true);
    bed.Frame();   // instantiate + onStart + first onUpdate, all this tick
    CHECK(bed.scene.GetEntityName(e) == StringView(u8"started"));
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 1.0f));   // 2.0 * 0.5
    bed.Frame();
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 2.0f));
    CHECK(bed.host.IsActive());
    CHECK(bed.scripts->InstanceCount() == 1u);
}

TEST_CASE("script.scene: harvested defaults apply; hash-keyed overrides win")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> mover = MakeClass(u8"Mover",
        u8"class Mover {\n"
        u8"    construct new(entity) {\n"
        u8"        _entity = entity\n"
        u8"        _speed = 0.0\n"
        u8"    }\n"
        u8"    speed=(v) { _speed = v }\n"
        u8"    onUpdate(dt) {\n"
        u8"        var p = _entity.position()\n"
        u8"        _entity.setPosition(p.x + _speed * dt, p.y, p.z)\n"
        u8"    }\n"
        u8"}\n",
        { u8"onUpdate" }, { FloatProperty(u8"speed", 2.0) });

    const dscene::EntityHandle defaulted = bed.AddScripted(mover, u8"defaulted");
    const dscene::EntityHandle overridden = bed.AddScripted(mover, u8"overridden");
    {
        ScriptComponent* c = bed.components->Get(overridden);
        ScriptPropertyValue ten;
        ten.kind = ScriptPropertyType::Float;
        ten.number = 10.0;
        c->behaviors[0].SetOverride(ScriptPropertyNameHash(u8"speed"), ten);
    }

    bed.Start();
    bed.Frame();   // dt 0.5
    CHECK(Near(bed.scene.GetLocalTransform(defaulted).position.x, 1.0f));   // default 2
    CHECK(Near(bed.scene.GetLocalTransform(overridden).position.x, 5.0f));  // override 10
}

TEST_CASE("script.scene: enable/disable edges dispatch onEnable/onDisable; disabled "
          "behaviors do not tick")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> toggler = MakeClass(u8"Toggler",
        u8"class Toggler {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onEnable() { _entity.setName(_entity.name() + \"+on\") }\n"
        u8"    onDisable() { _entity.setName(_entity.name() + \"+off\") }\n"
        u8"    onUpdate(dt) {\n"
        u8"        var p = _entity.position()\n"
        u8"        _entity.setPosition(p.x + 1, p.y, p.z)\n"
        u8"    }\n"
        u8"}\n",
        { u8"onEnable", u8"onDisable", u8"onUpdate" });

    const dscene::EntityHandle e = bed.AddScripted(toggler, u8"t");
    bed.Start();
    bed.Frame();
    CHECK(bed.scene.GetEntityName(e) == StringView(u8"t+on"));
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 1.0f));

    bed.components->Get(e)->behaviors[0].enabled = false;
    bed.Frame();   // delivers onDisable, skips onUpdate
    CHECK(bed.scene.GetEntityName(e) == StringView(u8"t+on+off"));
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 1.0f));
    bed.Frame();   // stays off
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 1.0f));

    bed.components->Get(e)->behaviors[0].enabled = true;
    bed.Frame();   // onEnable again + ticks
    CHECK(bed.scene.GetEntityName(e) == StringView(u8"t+on+off+on"));
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 2.0f));
}

TEST_CASE("script.scene: onDestroy fires on entity destroy AND on scene stop; stop "
          "releases every instance")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> counter = MakeClass(u8"Counter",
        u8"var DestroyCount = 0\n"
        u8"class Counter {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onUpdate(dt) {}\n"
        u8"    onDestroy() { DestroyCount = DestroyCount + 1 }\n"
        u8"}\n",
        { u8"onUpdate", u8"onDestroy" });

    const dscene::EntityHandle a = bed.AddScripted(counter, u8"a");
    (void)bed.AddScripted(counter, u8"b");
    bed.Start();
    bed.Frame();
    CHECK(bed.scripts->InstanceCount() == 2u);

    bed.scene.DestroyEntity(a);
    bed.Frame();
    CHECK(bed.scripts->InstanceCount() == 1u);
    {
        const Variant count = bed.host.Context()->GetGlobal(u8"DestroyCount");
        REQUIRE(count.TryGet<f64>() != nullptr);
        CHECK(*count.TryGet<f64>() == 1.0);
    }

    bed.scene.Stop();
    CHECK(bed.scripts->InstanceCount() == 0u);
    {
        const Variant count = bed.host.Context()->GetGlobal(u8"DestroyCount");
        REQUIRE(count.TryGet<f64>() != nullptr);
        CHECK(*count.TryGet<f64>() == 2.0);
    }
}

TEST_CASE("script.scene: a faulting behavior is disabled and logged; siblings keep "
          "running")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> faulty = MakeClass(u8"Faulty",
        u8"class Faulty {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onUpdate(dt) { Fiber.abort(\"boom\") }\n"
        u8"}\n",
        { u8"onUpdate" });
    RefPtr<ScriptClass> steady = MakeClass(u8"Steady",
        u8"class Steady {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onUpdate(dt) {\n"
        u8"        var p = _entity.position()\n"
        u8"        _entity.setPosition(p.x + 1, p.y, p.z)\n"
        u8"    }\n"
        u8"}\n",
        { u8"onUpdate" });

    const dscene::EntityHandle e = bed.scene.CreateEntity(u8"both");
    ScriptComponent& c = bed.components->Add(e);
    {
        ScriptBehavior first;
        first.script = faulty;
        c.behaviors.PushBack(Move(first));
        ScriptBehavior second;
        second.script = steady;
        c.behaviors.PushBack(Move(second));
    }

    bed.Start();
    bed.Frame();
    bed.Frame();
    ScriptComponent* live = bed.components->Get(e);
    CHECK(live->behaviors[0].faulted);                              // disabled after the fault
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 2.0f));   // sibling unaffected
}

TEST_CASE("script.scene: hot reload - product swap re-instantiates, re-applies "
          "overrides, resets transient state")
{
    ScriptedScene bed;
    const char8_t* moverV1 =
        u8"class Mover {\n"
        u8"    construct new(entity) {\n"
        u8"        _entity = entity\n"
        u8"        _speed = 0.0\n"
        u8"        _ticks = 0\n"
        u8"    }\n"
        u8"    speed=(v) { _speed = v }\n"
        u8"    onStart() { _entity.setName(_entity.name() + \"+start\") }\n"
        u8"    onUpdate(dt) {\n"
        u8"        _ticks = _ticks + 1\n"
        u8"        var p = _entity.position()\n"
        u8"        _entity.setPosition(p.x + _speed * dt, p.y, p.z)\n"
        u8"    }\n"
        u8"}\n";
    // v2: same convention, DOUBLE speed effect - observable difference after reload.
    const char8_t* moverV2 =
        u8"class Mover {\n"
        u8"    construct new(entity) {\n"
        u8"        _entity = entity\n"
        u8"        _speed = 0.0\n"
        u8"    }\n"
        u8"    speed=(v) { _speed = v }\n"
        u8"    onStart() { _entity.setName(_entity.name() + \"+restart\") }\n"
        u8"    onUpdate(dt) {\n"
        u8"        var p = _entity.position()\n"
        u8"        _entity.setPosition(p.x + 2 * _speed * dt, p.y, p.z)\n"
        u8"    }\n"
        u8"}\n";

    RefPtr<ScriptClass> v1 = MakeClass(u8"Mover", moverV1,
        { u8"onStart", u8"onUpdate" }, { FloatProperty(u8"speed", 1.0) });

    const dscene::EntityHandle e = bed.AddScripted(v1, u8"m");
    {
        ScriptPropertyValue three;
        three.kind = ScriptPropertyType::Float;
        three.number = 3.0;
        bed.components->Get(e)->behaviors[0].SetOverride(ScriptPropertyNameHash(u8"speed"),
                                                         three);
    }
    bed.Start();
    bed.Frame();   // dt 0.5: x = 1.5 (override 3)
    CHECK(bed.scene.GetEntityName(e) == StringView(u8"m+start"));
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 1.5f));

    // The cook swaps the product (here: the Ref's direct object). Same class name.
    RefPtr<ScriptClass> v2 = MakeClass(u8"Mover", moverV2,
        { u8"onStart", u8"onUpdate" }, { FloatProperty(u8"speed", 1.0) });
    bed.components->Get(e)->behaviors[0].script = v2;

    bed.Frame();   // re-instantiate (fresh state, onStart again), override RE-APPLIED
    CHECK(bed.scene.GetEntityName(e) == StringView(u8"m+start+restart"));
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 4.5f));   // +2*3*0.5
    CHECK(bed.components->Get(e)->behaviors[0].boundClass == v2.Get());
}

TEST_CASE("script.scene: entity-typed properties resolve guids to live entity handles "
          "(the facade crosses the boundary)")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> chaser = MakeClass(u8"Chaser",
        u8"class Chaser {\n"
        u8"    construct new(entity) {\n"
        u8"        _entity = entity\n"
        u8"        _target = null\n"
        u8"    }\n"
        u8"    target=(v) { _target = v }\n"
        u8"    onUpdate(dt) {\n"
        u8"        if (_target != null) {\n"
        u8"            var t = _target.position()\n"
        u8"            _entity.setPosition(t.x, t.y, t.z)\n"
        u8"        }\n"
        u8"    }\n"
        u8"}\n",
        { u8"onUpdate" }, { EntityProperty(u8"target") });

    const dscene::EntityHandle goal = bed.scene.CreateEntity(u8"goal");
    bed.scene.SetLocalPosition(goal, Float3{ 7.0f, 8.0f, 9.0f });
    const dscene::EntityHandle e = bed.AddScripted(chaser, u8"chaser");
    {
        ScriptPropertyValue target;
        target.kind = ScriptPropertyType::Entity;
        target.guid = bed.scene.GetEntityId(goal);
        bed.components->Get(e)->behaviors[0].SetOverride(ScriptPropertyNameHash(u8"target"),
                                                         target);
    }
    bed.Start();
    bed.Frame();
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 7.0f));
    CHECK(Near(bed.scene.GetLocalTransform(e).position.z, 9.0f));
}

TEST_CASE("script.scene: Log/Time/Random facades are callable (service-bound per run)")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> user = MakeClass(u8"FacadeUser",
        u8"class FacadeUser {\n"
        u8"    construct new(entity) {\n"
        u8"        _entity = entity\n"
        u8"        _ticks = 0\n"
        u8"    }\n"
        u8"    onUpdate(dt) {\n"
        u8"        _ticks = _ticks + 1\n"
        u8"        Log.info(\"tick at %(Time.now()) delta %(Time.delta())\")\n"
        u8"        var r = Random.range(1.0, 2.0)\n"
        u8"        if (r < 1.0 || r >= 2.0) { Fiber.abort(\"range broken\") }\n"
        u8"        // The run clock starts WITH the context - meaningful from tick 2 on.\n"
        u8"        if (_ticks > 1 && Time.delta() <= 0) { Fiber.abort(\"delta broken\") }\n"
        u8"        if (_ticks > 1 && Time.now() <= 0) { Fiber.abort(\"clock broken\") }\n"
        u8"        _entity.setPosition(Random.intRange(4, 4), 0, 0)\n"
        u8"    }\n"
        u8"}\n",
        { u8"onUpdate" });

    const dscene::EntityHandle e = bed.AddScripted(user, u8"f");
    bed.Start();
    bed.Frame();
    bed.Frame();
    ScriptComponent* c = bed.components->Get(e);
    CHECK_FALSE(c->behaviors[0].faulted);
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 4.0f));
}

TEST_CASE("script.scene: component wire round-trips through SerializeScene (script "
          "refs, enabled flags, every override kind; symmetric + count-guarded)")
{
    RegisterScriptComponentReflection();
    dscene::Scene scene(u8"wire");
    auto* manager = scene.AddSystem<ScriptComponentManager>();

    const dscene::EntityHandle e = scene.CreateEntity(u8"scripted");
    ScriptComponent& c = manager->Add(e);
    {
        ScriptBehavior first;
        first.script.SetId(Guid{ 0x11, 0x22 });
        first.enabled = false;
        ScriptPropertyValue speed;
        speed.kind = ScriptPropertyType::Float;
        speed.number = 12.5;
        first.SetOverride(ScriptPropertyNameHash(u8"speed"), speed);
        ScriptPropertyValue label;
        label.kind = ScriptPropertyType::String;
        label.text = String(u8"hello");
        first.SetOverride(ScriptPropertyNameHash(u8"label"), label);
        ScriptPropertyValue tint;
        tint.kind = ScriptPropertyType::Color;
        tint.color = Color{ 0.1f, 0.2f, 0.3f, 0.4f };
        first.SetOverride(ScriptPropertyNameHash(u8"tint"), tint);
        c.behaviors.PushBack(Move(first));

        ScriptBehavior second;
        second.script.SetId(Guid{ 0x33, 0x44 });
        ScriptPropertyValue target;
        target.kind = ScriptPropertyType::Entity;
        target.guid = Guid{ 0x55, 0x66 };
        second.SetOverride(ScriptPropertyNameHash(u8"target"), target);
        ScriptPropertyValue vec;
        vec.kind = ScriptPropertyType::Vec3;
        vec.vector = Float3{ 1, 2, 3 };
        second.SetOverride(ScriptPropertyNameHash(u8"offset"), vec);
        c.behaviors.PushBack(Move(second));
    }
    const Guid entityId = scene.GetEntityId(e);

    MemoryStream stream;
    {
        BinarySerializer w(stream, SerializeMode::Write);
        dscene::SerializeScene(w, scene);
        REQUIRE(w.IsOk());
    }
    (void)stream.Seek(0, SeekOrigin::Begin);

    dscene::Scene loaded(u8"loaded");
    auto* loadedManager = loaded.AddSystem<ScriptComponentManager>();
    {
        BinarySerializer r(stream, SerializeMode::Read);
        dscene::SerializeScene(r, loaded);
        REQUIRE(r.IsOk());
    }
    const dscene::EntityHandle le = loaded.FindEntity(entityId);
    REQUIRE(le.IsAssigned());
    ScriptComponent* lc = loadedManager->Get(le);
    REQUIRE(lc != nullptr);
    REQUIRE(lc->behaviors.Size() == 2u);

    const ScriptBehavior& b0 = lc->behaviors[0];
    CHECK(b0.script.id == (Guid{ 0x11, 0x22 }));
    CHECK_FALSE(b0.enabled);
    REQUIRE(b0.overrides.Size() == 3u);
    const ScriptPropertyOverride* speed = b0.FindOverride(ScriptPropertyNameHash(u8"speed"));
    REQUIRE(speed != nullptr);
    CHECK(speed->value.kind == ScriptPropertyType::Float);
    CHECK(speed->value.number == doctest::Approx(12.5));
    const ScriptPropertyOverride* label = b0.FindOverride(ScriptPropertyNameHash(u8"label"));
    REQUIRE(label != nullptr);
    CHECK(label->value.text == u8"hello");
    const ScriptPropertyOverride* tint = b0.FindOverride(ScriptPropertyNameHash(u8"tint"));
    REQUIRE(tint != nullptr);
    CHECK(tint->value.color.b == doctest::Approx(0.3f));

    const ScriptBehavior& b1 = lc->behaviors[1];
    CHECK(b1.script.id == (Guid{ 0x33, 0x44 }));
    CHECK(b1.enabled);
    const ScriptPropertyOverride* target = b1.FindOverride(ScriptPropertyNameHash(u8"target"));
    REQUIRE(target != nullptr);
    CHECK(target->value.guid == (Guid{ 0x55, 0x66 }));
    const ScriptPropertyOverride* offset = b1.FindOverride(ScriptPropertyNameHash(u8"offset"));
    REQUIRE(offset != nullptr);
    CHECK(offset->value.vector.y == doctest::Approx(2.0f));

    // Runtime fields never travel the wire.
    CHECK(b0.instance.Get() == nullptr);
    CHECK(b0.boundClass == nullptr);
    CHECK_FALSE(b0.started);
}

TEST_CASE("script.scene: a prefab-instance override on a behavior property round-trips "
          "(component-granular deltas - zero script-specific code)")
{
    RegisterScriptComponentReflection();

    // Author the prefab: one entity with a scripted behavior, default speed.
    dscene::Scene author(u8"author");
    auto* authorScripts = author.AddSystem<ScriptComponentManager>();
    const dscene::EntityHandle root = author.CreateEntity(u8"Bot");
    {
        ScriptComponent& c = authorScripts->Add(root);
        ScriptBehavior behavior;
        behavior.script.SetId(Guid{ 0xAA, 0x01 });
        c.behaviors.PushBack(Move(behavior));
    }
    MemoryStream payload;
    REQUIRE(dscene::CapturePrefab(author, root, payload).IsOk());

    // Level: two instances; ONE overrides the behavior's speed.
    dscene::Scene level(u8"level");
    auto* levelScripts = level.AddSystem<ScriptComponentManager>();
    (void)payload.Seek(0, SeekOrigin::Begin);
    const Guid prefabId{ 0xBB, 0x02 };
    const dscene::EntityHandle inst1 = dscene::SpawnPrefab(level, payload, prefabId);
    (void)payload.Seek(0, SeekOrigin::Begin);
    const dscene::EntityHandle inst2 = dscene::SpawnPrefab(level, payload, prefabId);
    REQUIRE(inst1.IsAssigned());
    REQUIRE(inst2.IsAssigned());
    {
        ScriptPropertyValue nine;
        nine.kind = ScriptPropertyType::Float;
        nine.number = 9.0;
        levelScripts->Get(inst1)->behaviors[0].SetOverride(ScriptPropertyNameHash(u8"speed"),
                                                           nine);
    }
    const Guid inst1Id = level.GetEntityId(inst1);
    const Guid inst2Id = level.GetEntityId(inst2);

    // Save -> load -> resolve prefabs (instances live as ref+deltas on the wire).
    MemoryStream saved;
    {
        BinarySerializer w(saved, SerializeMode::Write);
        dscene::SerializeScene(w, level);
        REQUIRE(w.IsOk());
    }
    dscene::Scene loaded(u8"loaded");
    auto* loadedScripts = loaded.AddSystem<ScriptComponentManager>();
    (void)saved.Seek(0, SeekOrigin::Begin);
    {
        BinarySerializer r(saved, SerializeMode::Read);
        dscene::SerializeScene(r, loaded, &saved);
        REQUIRE(r.IsOk());
    }
    const Span<const byte> payloadBytes = payload.Bytes();
    dscene::ResolveScenePrefabs(loaded, Function<UniquePtr<IStream>(const Guid&)>{
        [&payloadBytes, prefabId](const Guid& id) -> UniquePtr<IStream> {
            if (id != prefabId) { return UniquePtr<IStream>{}; }
            auto stream = MakeUnique<MemoryStream>(DefaultAllocator());
            (void)stream->Write(payloadBytes.Data(), payloadBytes.Size());
            (void)stream->Seek(0, SeekOrigin::Begin);
            return UniquePtr<IStream>(stream.Release(), DefaultAllocator());
        } });

    const dscene::EntityHandle l1 = loaded.FindEntity(inst1Id);
    const dscene::EntityHandle l2 = loaded.FindEntity(inst2Id);
    REQUIRE(l1.IsAssigned());
    REQUIRE(l2.IsAssigned());
    ScriptComponent* c1 = loadedScripts->Get(l1);
    ScriptComponent* c2 = loadedScripts->Get(l2);
    REQUIRE(c1 != nullptr);
    REQUIRE(c2 != nullptr);
    REQUIRE(c1->behaviors.Size() == 1u);
    REQUIRE(c2->behaviors.Size() == 1u);

    // The override survived on instance 1; instance 2 still tracks the template.
    const ScriptPropertyOverride* over =
        c1->behaviors[0].FindOverride(ScriptPropertyNameHash(u8"speed"));
    REQUIRE(over != nullptr);
    CHECK(over->value.number == doctest::Approx(9.0));
    CHECK(c2->behaviors[0].overrides.IsEmpty());
    CHECK(c1->behaviors[0].script.id == (Guid{ 0xAA, 0x01 }));
}

TEST_CASE("script.scene: run-host teardown after stop releases the context; a language "
          "without a backend refuses cleanly")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> noop = MakeClass(u8"Noop",
        u8"class Noop {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onUpdate(dt) {}\n"
        u8"}\n",
        { u8"onUpdate" });
    (void)bed.AddScripted(noop, u8"n");
    bed.Start();
    bed.Frame();
    CHECK(bed.host.IsActive());
    bed.scene.Stop();
    CHECK(bed.scripts->InstanceCount() == 0u);
    bed.host.Teardown();   // the subsystem's MaybeTeardown does this in-engine
    CHECK_FALSE(bed.host.IsActive());

    // Behaviors in an unregistered language stay disabled with a clean error.
    RefPtr<ScriptClass> alien = MakeClass(u8"Alien", u8"whatever", { u8"onUpdate" });
    alien->language = String(u8"nolang");
    ScriptRunHost host;
    CHECK(host.EnsureContext(u8"nolang") == nullptr);
    CHECK_FALSE(host.EnsureClassLoaded(*alien));
}

TEST_CASE("script.scene: entity.send invokes on<Message>(arg) on every declaring "
          "behavior of the target (P2 messaging)")
{
    ScriptedScene bed;

    // Receiver at index 0 so it is instantiated before the sender (index 1) fires its
    // onStart send this same tick - dispatch skips not-yet-live instances by design.
    RefPtr<ScriptClass> receiver = MakeClass(u8"Receiver",
        u8"class Receiver {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onPing(amount) { _entity.setName(\"pinged:\" + amount.toString) }\n"
        u8"}\n",
        { u8"onPing" });
    RefPtr<ScriptClass> sender = MakeClass(u8"Sender",
        u8"class Sender {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onStart() { _entity.send(\"ping\", 7) }\n"
        u8"}\n",
        { u8"onStart" });

    dscene::EntityHandle target = bed.scene.CreateEntity(u8"target");
    ScriptComponent& component = bed.components->Add(target);
    { ScriptBehavior b; b.script = receiver; component.behaviors.PushBack(Move(b)); }
    { ScriptBehavior b; b.script = sender; component.behaviors.PushBack(Move(b)); }

    bed.Start();
    bed.Frame();   // both instantiate; sender.onStart -> send "ping" -> receiver.onPing

    CHECK(bed.scene.GetEntityName(target) == u8"pinged:7");
}

TEST_CASE("script.scene: entity.send to a target with no matching handler is a safe "
          "no-op (P2 messaging)")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> sender = MakeClass(u8"LoneSender",
        u8"class LoneSender {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onStart() { _entity.send(\"noHandler\", 1) }\n"
        u8"}\n",
        { u8"onStart" });
    dscene::EntityHandle e = bed.AddScripted(sender, u8"lone");
    bed.Start();
    bed.Frame();   // must not fault the sender
    ScriptComponent* c = bed.components->Get(e);
    REQUIRE(c != nullptr);
    CHECK_FALSE(c->behaviors[0].faulted);
}

TEST_CASE("script.scene: updateInterval throttles onUpdate and delivers the accumulated "
          "dt (P3)")
{
    ScriptedScene bed;
    RefPtr<ScriptClass> ticker = MakeClass(u8"Ticker",
        u8"class Ticker {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onUpdate(dt) {\n"
        u8"        var p = _entity.position()\n"
        u8"        _entity.setPosition(p.x + 1.0, dt, p.z)\n"   // x counts calls; y = last dt
        u8"    }\n"
        u8"}\n",
        { u8"onUpdate" });
    const dscene::EntityHandle e = bed.AddScripted(ticker, u8"ticker");
    bed.components->Get(e)->behaviors[0].updateInterval = 1.0f;

    bed.Start();
    bed.Frame(0.5f);   // acc 0.5 < 1.0 -> no update
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 0.0f));
    bed.Frame(0.5f);   // acc 1.0 -> ONE update, dt = accumulated 1.0
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 1.0f));
    CHECK(Near(bed.scene.GetLocalTransform(e).position.y, 1.0f));   // accumulated dt, not 0.5
    bed.Frame(0.5f);   // acc 0.5 -> no update
    bed.Frame(0.5f);   // acc 1.0 -> second update
    CHECK(Near(bed.scene.GetLocalTransform(e).position.x, 2.0f));   // 2 calls over 4 ticks
}

TEST_CASE("script.scene: updateInterval survives the SerializeScene wire (P3 symmetry)")
{
    RegisterScriptComponentReflection();
    dscene::Scene scene(u8"interval-wire");
    auto* manager = scene.AddSystem<ScriptComponentManager>();
    const dscene::EntityHandle e = scene.CreateEntity(u8"scripted");
    ScriptComponent& c = manager->Add(e);
    ScriptBehavior behavior;
    behavior.script.SetId(Guid{ 0x77, 0x88 });
    behavior.updateInterval = 0.25f;
    c.behaviors.PushBack(Move(behavior));
    const Guid entityId = scene.GetEntityId(e);

    MemoryStream stream;
    {
        BinarySerializer w(stream, SerializeMode::Write);
        dscene::SerializeScene(w, scene);
        REQUIRE(w.IsOk());
    }
    (void)stream.Seek(0, SeekOrigin::Begin);

    dscene::Scene loaded(u8"loaded");
    auto* loadedManager = loaded.AddSystem<ScriptComponentManager>();
    {
        BinarySerializer r(stream, SerializeMode::Read);
        dscene::SerializeScene(r, loaded);
        REQUIRE(r.IsOk());
    }
    ScriptComponent* lc = loadedManager->Get(loaded.FindEntity(entityId));
    REQUIRE(lc != nullptr);
    REQUIRE(lc->behaviors.Size() == 1u);
    CHECK(Near(lc->behaviors[0].updateInterval, 0.25f));
}

TEST_CASE("script.scene: Scene.spawn routes through the run spawner to the current scene "
          "and returns a live Entity (P2)")
{
    ScriptedScene bed;

    // A fake prefab spawner: creates a plain entity at the requested position (a real run
    // resolves + spawns a cooked prefab payload; the facade->binding->scene path is what
    // is under test here). Records the last call.
    int spawnCalls = 0;
    Guid lastPrefab;
    dscene::EntityHandle spawnedHandle;
    bed.host.Binding().spawnPrefab =
        Function<dscene::EntityHandle(dscene::Scene*, const Guid&, const Float3&)>{
            [&](dscene::Scene* scene, const Guid& prefabId, const Float3& position)
                -> dscene::EntityHandle {
                ++spawnCalls;
                lastPrefab = prefabId;
                spawnedHandle = scene->CreateEntity(u8"spawned");
                scene->SetLocalPosition(spawnedHandle, position);
                return spawnedHandle;
            } };

    // The behavior spawns on start using a prefab id delivered as an asset property.
    RefPtr<ScriptClass> spawner = MakeClass(u8"Spawner",
        u8"class Spawner {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    prefab=(v) { _prefab = v }\n"
        u8"    onStart() {\n"
        u8"        var e = Scene.spawn(_prefab, 3.0, 4.0, 5.0)\n"
        u8"        e.setName(\"child\")\n"
        u8"    }\n"
        u8"}\n",
        { u8"onStart" });
    ScriptPropertyDesc prefabProp;
    prefabProp.name = String(u8"prefab");
    prefabProp.hash = ScriptPropertyNameHash(u8"prefab");
    prefabProp.type = ScriptPropertyType::Asset;
    prefabProp.assetType = String(u8"Prefab");
    prefabProp.defaultValue.kind = ScriptPropertyType::Asset;
    prefabProp.defaultValue.guid = Guid{ 0xABC, 0xDEF };
    spawner->properties.PushBack(prefabProp);

    const dscene::EntityHandle e = bed.AddScripted(spawner, u8"spawner");
    (void)e;
    bed.Start();
    bed.Frame();

    CHECK(spawnCalls == 1);
    CHECK(lastPrefab == Guid{ 0xABC, 0xDEF });
    // Scene.spawn returned the live Entity: the script renamed it and it sits at the
    // requested world position.
    REQUIRE(spawnedHandle.IsAssigned());
    CHECK(bed.scene.GetEntityName(spawnedHandle) == StringView(u8"child"));
    CHECK(Near(bed.scene.GetLocalTransform(spawnedHandle).position.x, 3.0f));
    CHECK(Near(bed.scene.GetLocalTransform(spawnedHandle).position.z, 5.0f));
}

TEST_CASE("script.scene: Scene.find / Scene.findByPath resolve entities in the current "
          "scene from a behavior (P2)")
{
    ScriptedScene bed;
    // Build a small hierarchy the behavior will look up: Target (root) and Player/Weapon.
    dscene::EntityHandle target = bed.scene.CreateEntity(u8"Target");
    dscene::EntityHandle player = bed.scene.CreateEntity(u8"Player");
    dscene::EntityHandle weapon = bed.scene.CreateEntity(u8"Weapon");
    bed.scene.SetParent(weapon, player);
    (void)target;

    RefPtr<ScriptClass> finder = MakeClass(u8"Finder",
        u8"class Finder {\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    onStart() {\n"
        u8"        var t = Scene.find(\"Target\")\n"
        u8"        if (t.isValid()) { t.setName(\"found-by-name\") }\n"
        u8"        var w = Scene.findByPath(\"Player/Weapon\")\n"
        u8"        if (w.isValid()) { w.setName(\"found-by-path\") }\n"
        u8"        var missing = Scene.find(\"Nope\")\n"
        u8"        if (!missing.isValid()) { _entity.setName(\"miss-ok\") }\n"
        u8"    }\n"
        u8"}\n",
        { u8"onStart" });
    const dscene::EntityHandle e = bed.AddScripted(finder, u8"finder");
    bed.Start();
    bed.Frame();

    CHECK(bed.scene.GetEntityName(target) == StringView(u8"found-by-name"));
    CHECK(bed.scene.GetEntityName(weapon) == StringView(u8"found-by-path"));
    CHECK(bed.scene.GetEntityName(e) == StringView(u8"miss-ok"));
}
