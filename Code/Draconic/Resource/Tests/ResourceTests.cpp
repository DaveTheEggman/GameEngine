#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;

using namespace draconic::core;
using namespace draconic::vfs;
using namespace draconic::resource;

namespace
{
    // Source: serializable, lives in the content database. Carries editor-only
    // data (editorNote) that the runtime product must NOT inherit.
    class MaterialResource final : public ISerializable
    {
        DRACONIC_OBJECT(MaterialResource, ISerializable)
    public:
        i32 shininess = 0;
        String shader;
        String editorNote;   // editor-only

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "shininess", shininess);
            draconic::core::Serialize(ar, "shader", shader);
            draconic::core::Serialize(ar, "editorNote", editorNote);
        }
    };

    // Product: lean runtime object built from the source. No editorNote.
    class Material final : public Object
    {
        DRACONIC_OBJECT(Material, Object)
    public:
        f32 specular = 0.0f;
        String shader;
    };

    // Factory: builds a Material product from a MaterialResource source.
    class MaterialFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override { return &Material::StaticType(); }

        int builds = 0;                  // observe rebuilds (incl. dependency-propagated reloads)
        HashMap<Guid, Guid> bindMap;     // when building key, Bind value (a child) -> auto-edge

        [[nodiscard]] RefPtr<Object> Create(ResourceManager& manager, draconic::content::Instance& instance) override
        {
            ++builds;
            // Resolving a child via the manager mid-build auto-records a dependency.
            if (Guid* child = bindMap.Find(instance.Id())) { (void)manager.Bind(Material::StaticType(), *child); }
            RefPtr<ISerializable> source = instance.ReadObject();
            MaterialResource* res = Cast<MaterialResource>(source.Get());
            if (res == nullptr) { return RefPtr<Object>{}; }

            RefPtr<Material> material = MakeRef<Material>(DefaultAllocator());
            material->specular = static_cast<f32>(res->shininess) / 128.0f;
            material->shader = res->shader;
            // editorNote is intentionally dropped - the product is runtime-only.
            return material;
        }
    };

    void RemoveTree()
    {
        FileDelete(u8"draconic_resource_test_db/steel.rasset");
        RemoveDirectory(u8"draconic_resource_test_db");
    }

    void WriteSource(draconic::content::ContentDatabase& db, const Guid& id, i32 shininess, StringView shader)
    {
        auto* instance = db.GetInstance(id);
        REQUIRE(instance != nullptr);
        MaterialResource r;
        r.shininess = shininess;
        r.shader = String(shader);
        r.editorNote = u8"node@(10,20)";
        REQUIRE(instance->WriteObject(r).IsOk());
    }
}

DRACONIC_DEFINE_OBJECT(MaterialResource, "draconic::resource::test")
DRACONIC_DEFINE_OBJECT(Material, "draconic::resource::test")

TEST_CASE("resource: bind builds a product from a source, with caching")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    RemoveTree();
    NativeFileSystem mount(u8"draconic_resource_test_db");

    Guid id;
    {
        draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
        auto* steel = db.RootGroup()->CreateInstance(u8"steel", MaterialResource::StaticType());
        id = steel->Id();
        WriteSource(db, id, 64, u8"pbr");
    }

    draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
    MaterialFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    // Bind: source -> product. Product is derived/lean (no editorNote field).
    Proxy<Material> p = manager.Bind<Material>(id);
    REQUIRE(p);
    CHECK(p->shader == u8"pbr");
    CHECK(p->specular == 0.5f);   // 64 / 128

    // Cache: binding the same id returns the same handle.
    Proxy<Material> p2 = manager.Bind<Material>(id);
    CHECK(p2.Handle() == p.Handle());

    // Unknown id -> invalid proxy.
    Proxy<Material> none = manager.Bind<Material>(Guid{ 1, 2 });
    CHECK_FALSE(none);

    // Flush drops the product; the proxy follows the handle and goes invalid,
    // then a rebind rebuilds into the same handle with a fresh product.
    CHECK(manager.Flush(id));
    CHECK_FALSE(p);
    Proxy<Material> p3 = manager.Bind<Material>(id);
    CHECK(p3.Handle() == p.Handle());
    REQUIRE(p);                    // p recovers through the shared handle
    CHECK(p->specular == 0.5f);    // rebuilt correctly

    RemoveTree();
}

TEST_CASE("resource: reload rebuilds the product and proxies see the new value")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    RemoveTree();
    NativeFileSystem mount(u8"draconic_resource_test_db");

    Guid id;
    {
        draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
        auto* steel = db.RootGroup()->CreateInstance(u8"steel", MaterialResource::StaticType());
        id = steel->Id();
        WriteSource(db, id, 64, u8"pbr");
    }

    draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
    MaterialFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Material> p = manager.Bind<Material>(id);
    REQUIRE(p);
    CHECK(p->specular == 0.5f);

    // Source changes on disk; Reload rebuilds the product behind the handle.
    WriteSource(db, id, 128, u8"pbr2");
    CHECK(manager.Reload(id));
    CHECK(p->specular == 1.0f);    // same proxy, new product
    CHECK(p->shader == u8"pbr2");

    RemoveTree();
}

namespace
{
    // Cleanup for the dependency tests (its own db dir; one .rasset per instance).
    void RemoveDepTree()
    {
        const StringView names[] = { u8"a", u8"b", u8"c", u8"parent", u8"child" };
        for (StringView n : names)
        {
            String f = String(u8"draconic_resource_dep_db/");
            f.Append(n);
            f.Append(u8".rasset");
            FileDelete(f.AsView());
        }
        RemoveDirectory(u8"draconic_resource_dep_db");
    }

    // Create an instance + write a MaterialResource source; returns its id.
    Guid MakeInstance(draconic::content::ContentDatabase& db, StringView name, i32 shininess)
    {
        auto* inst = db.RootGroup()->CreateInstance(name, MaterialResource::StaticType());
        WriteSource(db, inst->Id(), shininess, name);
        return inst->Id();
    }
}

TEST_CASE("resource: a factory-resolved child is an auto-recorded dependency")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    RemoveDepTree();
    NativeFileSystem mount(u8"draconic_resource_dep_db");

    Guid parentId, childId;
    {
        draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
        childId  = MakeInstance(db, u8"child", 64);
        parentId = MakeInstance(db, u8"parent", 32);
    }

    draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
    MaterialFactory factory;
    factory.bindMap.InsertOrAssign(parentId, childId);   // building parent Binds child
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Material> parent = manager.Bind<Material>(parentId);   // builds parent -> binds child
    REQUIRE(parent);
    CHECK(factory.builds == 2);                                  // parent + the child it pulled in

    Span<const Guid> deps = manager.Dependents(childId);         // edge was recorded
    REQUIRE(deps.Size() == 1u);
    CHECK(deps[0] == parentId);

    const int b0 = factory.builds;
    CHECK(manager.Reload(childId));                             // child reload propagates to parent
    CHECK(factory.builds == b0 + 2);                           // both rebuilt (child + dependent parent)
    REQUIRE(parent);                                           // proxy still valid after the swap

    RemoveDepTree();
}

TEST_CASE("resource: reload propagates transitively, each resource once")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    RemoveDepTree();
    NativeFileSystem mount(u8"draconic_resource_dep_db");

    Guid a, b, c;
    {
        draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
        a = MakeInstance(db, u8"a", 16);
        b = MakeInstance(db, u8"b", 32);
        c = MakeInstance(db, u8"c", 64);
    }

    draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
    MaterialFactory factory;
    factory.bindMap.InsertOrAssign(a, b);   // a -> b
    factory.bindMap.InsertOrAssign(b, c);   // b -> c
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Material> pa = manager.Bind<Material>(a);   // builds a -> b -> c
    REQUIRE(pa);
    CHECK(factory.builds == 3);

    const int b0 = factory.builds;
    CHECK(manager.Reload(c));               // c -> b -> a, each exactly once
    CHECK(factory.builds == b0 + 3);

    RemoveDepTree();
}

TEST_CASE("resource: a rebuild drops stale dependency edges")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    RemoveDepTree();
    NativeFileSystem mount(u8"draconic_resource_dep_db");

    Guid parentId, childId;
    {
        draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
        childId  = MakeInstance(db, u8"child", 64);
        parentId = MakeInstance(db, u8"parent", 32);
    }

    draconic::content::ContentDatabase db(mount, draconic::core::BinarySerializerFactory(), u8".rasset");
    MaterialFactory factory;
    factory.bindMap.InsertOrAssign(parentId, childId);
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Material> parent = manager.Bind<Material>(parentId);
    REQUIRE(parent);
    CHECK(manager.Dependents(childId).Size() == 1u);

    // Parent stops referencing the child; rebuilding parent must drop the edge.
    factory.bindMap.Remove(parentId);
    CHECK(manager.Reload(parentId));
    CHECK(manager.Dependents(childId).Size() == 0u);

    // Now a child reload rebuilds only the child.
    const int b0 = factory.builds;
    CHECK(manager.Reload(childId));
    CHECK(factory.builds == b0 + 1);

    RemoveDepTree();
}

TEST_CASE("resource: deserializing a ref drops the stale binding when the id changes")
{
    // A directly-bound ref (procedural or picker-assigned) reads a DIFFERENT id from a blob
    // (component paste / prefab revert): the old binding must not keep rendering. Reading
    // the SAME id keeps the binding (steady-state reload of an unchanged component).
    RefPtr<Material> live = MakeRef<Material>(DefaultAllocator());
    Ref<Material> ref;
    ref.SetDirect(RefPtr<Material>(live.Get()));
    ref.id = Guid{ 0x1, 0x1 };
    REQUIRE(ref.Get() == live.Get());

    // Write a ref whose id is NIL ("no resource"), then read it over the live one.
    MemoryStream buffer;
    {
        Ref<Material> cleared;
        BinarySerializer ar(buffer, SerializeMode::Write);
        Serialize(ar, cleared);
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    {
        BinarySerializer ar(buffer, SerializeMode::Read);
        Serialize(ar, ref);
    }
    CHECK(ref.id == Guid{});
    CHECK(ref.Get() == nullptr);   // the stale direct binding is gone

    // Same-id read keeps the binding.
    Ref<Material> stable;
    stable.SetDirect(RefPtr<Material>(live.Get()));
    stable.id = Guid{};
    (void)buffer.Seek(0, SeekOrigin::Begin);
    {
        BinarySerializer ar(buffer, SerializeMode::Read);
        Serialize(ar, stable);
    }
    CHECK(stable.Get() == live.Get());
}
