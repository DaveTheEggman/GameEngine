#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.xml.serialization;

using namespace draconic::core;
using namespace draconic::vfs;
using namespace draconic::content;

namespace
{
    class MaterialResource final : public ISerializable
    {
        DRACONIC_OBJECT(MaterialResource, ISerializable)
    public:
        i32 shininess = 0;
        String shader;

        void Serialize(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "shininess", shininess);
            draconic::core::Serialize(ar, "shader", shader);
        }
    };

    void RemoveTree(StringView root)
    {
        FileDelete(JoinPath(root, u8"materials/steel.rasset"));
        FileDelete(JoinPath(root, u8"materials/steel.xasset"));
        FileDelete(JoinPath(root, u8"materials/steel.extra.bin"));
        RemoveDirectory(JoinPath(root, u8"materials"));
        RemoveDirectory(root);
    }

    // Shared round-trip test body.
    void RunRoundTripTest(StringView dbDir, StringView ext,
                          SerializerFactory (*makeFactory)())
    {
        GlobalTypeRegistry().Register(MaterialResource::StaticType());
        RegisterSerializable<MaterialResource>();

        RemoveTree(dbDir);
        NativeFileSystem mount(dbDir);

        Guid steelId;
        const byte extra[] = { byte{ 0xAB }, byte{ 0xCD }, byte{ 0xEF } };

        // --- author ---
        {
            ContentDatabase db(mount, makeFactory(), ext);
            Group* materials = db.RootGroup()->CreateGroup(u8"materials");
            REQUIRE(materials != nullptr);

            draconic::content::Instance* steel = materials->CreateInstance(u8"steel", MaterialResource::StaticType());
            REQUIRE(steel != nullptr);
            steelId = steel->Id();
            CHECK(static_cast<bool>(steelId));
            CHECK(steel->Path() == u8"materials/steel");

            MaterialResource mat;
            mat.shininess = 64;
            mat.shader = u8"pbr/metal";
            CHECK(steel->WriteObject(mat).IsOk());
            CHECK(steel->WriteData(u8"extra", Span<const byte>{ extra, ArrayCount(extra) }).IsOk());
        }

        // --- reopen: a fresh database scans the mount from disk ---
        {
            ContentDatabase db(mount, makeFactory(), ext);

            Group* materials = db.RootGroup()->GetGroup(u8"materials");
            REQUIRE(materials != nullptr);
            REQUIRE(materials->GetInstance(u8"steel") != nullptr);

            draconic::content::Instance* byPath = db.GetInstance(u8"materials/steel");
            draconic::content::Instance* byGuid = db.GetInstance(steelId);
            REQUIRE(byPath != nullptr);
            CHECK(byPath == byGuid);
            CHECK(byPath->Id() == steelId);

            RefPtr<ISerializable> obj = db.ReadObject(steelId);
            REQUIRE(obj.Get() != nullptr);
            MaterialResource* mat = Cast<MaterialResource>(obj.Get());
            REQUIRE(mat != nullptr);
            CHECK(mat->shininess == 64);
            CHECK(mat->shader == u8"pbr/metal");

            UniquePtr<IStream> data = byPath->ReadData(u8"extra");
            REQUIRE(static_cast<bool>(data));
            byte buffer[3] = {};
            CHECK(data->Read(buffer, 3) == 3u);
            CHECK(buffer[0] == byte{ 0xAB });
            CHECK(buffer[2] == byte{ 0xEF });

            CHECK(db.GetInstance(u8"materials/nope") == nullptr);
            CHECK(db.GetInstance(Guid{ 1, 2 }) == nullptr);
        }

        RemoveTree(dbDir);
    }

    SerializerFactory MakeBinaryFactory() { return BinarySerializerFactory(); }
    SerializerFactory MakeXmlFactory() { return draconic::xml::XmlSerializerFactory(); }
}

DRACONIC_DEFINE_OBJECT(MaterialResource, "draconic::content::test")

TEST_CASE("content: binary round-trip")
{
    RunRoundTripTest(u8"draconic_content_test_db_bin", u8".rasset", MakeBinaryFactory);
}

TEST_CASE("content: XML round-trip")
{
    RunRoundTripTest(u8"draconic_content_test_db_xml", u8".xasset", MakeXmlFactory);
}
