#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.resource;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::resource;

namespace
{
    // Source: serializable, lives in the content database. Carries editor-only
    // data (editorNote) that the runtime product must NOT inherit.
    class MaterialResource final : public ISerializable
    {
        RAPTOR_OBJECT(MaterialResource, ISerializable)
    public:
        i32 shininess = 0;
        WideString shader;
        WideString editorNote;   // editor-only

        void Serialize(ISerializer& ar) override
        {
            raptor::core::Serialize(ar, "shininess", shininess);
            raptor::core::Serialize(ar, "shader", shader);
            raptor::core::Serialize(ar, "editorNote", editorNote);
        }
    };

    // Product: lean runtime object built from the source. No editorNote.
    class Material final : public Object
    {
        RAPTOR_OBJECT(Material, Object)
    public:
        f32 specular = 0.0f;
        WideString shader;
    };

    // Factory: builds a Material product from a MaterialResource source.
    class MaterialFactory final : public IResourceFactory
    {
    public:
        [[nodiscard]] const TypeInfo* ProductType() const override { return &Material::StaticType(); }

        [[nodiscard]] RefPtr<Object> Create(raptor::content::Instance& instance) override
        {
            RefPtr<ISerializable> source = instance.ReadObject();
            MaterialResource* res = Cast<MaterialResource>(source.Get());
            if (res == nullptr) { return RefPtr<Object>{}; }

            RefPtr<Material> material = MakeRef<Material>(DefaultAllocator());
            material->specular = static_cast<f32>(res->shininess) / 128.0f;
            material->shader = res->shader;
            // editorNote is intentionally dropped — the product is runtime-only.
            return material;
        }
    };

    void RemoveTree()
    {
        FileDelete(u"raptor_resource_test_db/steel.rasset");
        RemoveDirectory(u"raptor_resource_test_db");
    }

    void WriteSource(raptor::content::ContentDatabase& db, const Guid& id, i32 shininess, WideStringView shader)
    {
        auto* instance = db.GetInstance(id);
        REQUIRE(instance != nullptr);
        MaterialResource r;
        r.shininess = shininess;
        r.shader = WideString(shader);
        r.editorNote = u"node@(10,20)";
        REQUIRE(instance->WriteObject(r).IsOk());
    }
}

RAPTOR_DEFINE_OBJECT(MaterialResource, "raptor::resource::test")
RAPTOR_DEFINE_OBJECT(Material, "raptor::resource::test")

TEST_CASE("resource: bind builds a product from a source, with caching")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    RemoveTree();
    NativeFileSystem mount(u"raptor_resource_test_db");

    Guid id;
    {
        raptor::content::ContentDatabase db(mount);
        auto* steel = db.RootGroup()->CreateInstance(u"steel", MaterialResource::StaticType());
        id = steel->Id();
        WriteSource(db, id, 64, u"pbr");
    }

    raptor::content::ContentDatabase db(mount);
    MaterialFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    // Bind: source -> product. Product is derived/lean (no editorNote field).
    Proxy<Material> p = manager.Bind<Material>(id);
    REQUIRE(p);
    CHECK(p->shader == u"pbr");
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
    NativeFileSystem mount(u"raptor_resource_test_db");

    Guid id;
    {
        raptor::content::ContentDatabase db(mount);
        auto* steel = db.RootGroup()->CreateInstance(u"steel", MaterialResource::StaticType());
        id = steel->Id();
        WriteSource(db, id, 64, u"pbr");
    }

    raptor::content::ContentDatabase db(mount);
    MaterialFactory factory;
    ResourceManager manager(db);
    manager.AddFactory(&factory);

    Proxy<Material> p = manager.Bind<Material>(id);
    REQUIRE(p);
    CHECK(p->specular == 0.5f);

    // Source changes on disk; Reload rebuilds the product behind the handle.
    WriteSource(db, id, 128, u"pbr2");
    CHECK(manager.Reload(id));
    CHECK(p->specular == 1.0f);    // same proxy, new product
    CHECK(p->shader == u"pbr2");

    RemoveTree();
}
