#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import raptor.core;
import raptor.vfs;
import raptor.content;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::content;

namespace
{
    // A source resource: serializable, lives in the content database.
    class MaterialResource final : public ISerializable
    {
        RAPTOR_OBJECT(MaterialResource, ISerializable)
    public:
        i32 shininess = 0;
        String shader;

        void Serialize(ISerializer& ar) override
        {
            raptor::core::Serialize(ar, "shininess", shininess);
            raptor::core::Serialize(ar, "shader", shader);
        }
    };

    void RemoveTree()
    {
        FileDelete(u8"raptor_content_test_db/materials/steel.rasset");
        FileDelete(u8"raptor_content_test_db/materials/steel.extra.bin");
        RemoveDirectory(u8"raptor_content_test_db/materials");
        RemoveDirectory(u8"raptor_content_test_db");
    }
}

RAPTOR_DEFINE_OBJECT(MaterialResource, "raptor::content::test")

TEST_CASE("content: write source resource, reopen, read back by guid and path")
{
    // The load path needs the type both in the type registry (resolve by name)
    // and the serializable registry (construct by id).
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    RemoveTree();
    NativeFileSystem mount(u8"raptor_content_test_db");

    Guid steelId;
    const byte extra[] = { byte{ 0xAB }, byte{ 0xCD }, byte{ 0xEF } };

    // --- author ---
    {
        ContentDatabase db(mount);
        Group* materials = db.RootGroup()->CreateGroup(u8"materials");
        REQUIRE(materials != nullptr);

        raptor::content::Instance* steel = materials->CreateInstance(u8"steel", MaterialResource::StaticType());
        REQUIRE(steel != nullptr);
        steelId = steel->Id();
        CHECK(static_cast<bool>(steelId));                 // a fresh guid was assigned
        CHECK(steel->Path() == u8"materials/steel");

        MaterialResource mat;
        mat.shininess = 64;
        mat.shader = u8"pbr/metal";
        CHECK(steel->WriteObject(mat).IsOk());
        CHECK(steel->WriteData(u8"extra", Span<const byte>{ extra, ArrayCount(extra) }).IsOk());
    }

    // --- reopen: a fresh database scans the mount from disk ---
    {
        ContentDatabase db(mount);

        // Group tree rebuilt.
        Group* materials = db.RootGroup()->GetGroup(u8"materials");
        REQUIRE(materials != nullptr);
        REQUIRE(materials->GetInstance(u8"steel") != nullptr);

        // Lookup by path and by guid resolve to the same instance.
        raptor::content::Instance* byPath = db.GetInstance(u8"materials/steel");
        raptor::content::Instance* byGuid = db.GetInstance(steelId);
        REQUIRE(byPath != nullptr);
        CHECK(byPath == byGuid);
        CHECK(byPath->Id() == steelId);

        // Polymorphic read: reconstruct the concrete type and deserialize.
        RefPtr<ISerializable> obj = db.ReadObject(steelId);
        REQUIRE(obj.Get() != nullptr);
        MaterialResource* mat = Cast<MaterialResource>(obj.Get());
        REQUIRE(mat != nullptr);
        CHECK(mat->shininess == 64);
        CHECK(mat->shader == u8"pbr/metal");

        // Data stream round-trips.
        UniquePtr<IStream> data = byPath->ReadData(u8"extra");
        REQUIRE(static_cast<bool>(data));
        byte buffer[3] = {};
        CHECK(data->Read(buffer, 3) == 3u);
        CHECK(buffer[0] == byte{ 0xAB });
        CHECK(buffer[2] == byte{ 0xEF });

        // Missing lookups.
        CHECK(db.GetInstance(u8"materials/nope") == nullptr);
        CHECK(db.GetInstance(Guid{ 1, 2 }) == nullptr);
    }

    RemoveTree();
}
