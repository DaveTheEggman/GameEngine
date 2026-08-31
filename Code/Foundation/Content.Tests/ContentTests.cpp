// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.xml.serialization;

using namespace foundation::core;
using namespace foundation::vfs;
using namespace foundation::content;

namespace
{
    class MaterialResource final : public ISerializable
    {
        RTTI_OBJECT(MaterialResource, ISerializable)
    public:
        i32 shininess = 0;
        String shader;

        void Serialize(ISerializer& ar) override
        {
            foundation::core::Serialize(ar, "shininess", shininess);
            foundation::core::Serialize(ar, "shader", shader);
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
    void RunRoundTripTest(StringView dbDir, StringView ext, SerializerFactory (*makeFactory)())
    {
        GlobalTypeRegistry().Register(MaterialResource::StaticType());
        RegisterSerializable<MaterialResource>();

        RemoveTree(dbDir);
        NativeFileSystem mount(dbDir);

        Guid steelId;
        const byte extra[] = {byte{0xAB}, byte{0xCD}, byte{0xEF}};

        // --- author ---
        {
            ContentDatabase db(mount, makeFactory(), ext);
            Group* materials = db.RootGroup()->CreateGroup(u8"materials");
            REQUIRE(materials != nullptr);

            foundation::content::Instance* steel =
                materials->CreateInstance(u8"steel", MaterialResource::StaticType());
            REQUIRE(steel != nullptr);
            steelId = steel->Id();
            CHECK(static_cast<bool>(steelId));
            CHECK(steel->Path() == u8"materials/steel");

            MaterialResource mat;
            mat.shininess = 64;
            mat.shader = u8"pbr/metal";
            CHECK(steel->WriteObject(mat).IsOk());
            CHECK(steel->WriteData(u8"extra", Span<const byte>{extra, ArrayCount(extra)}).IsOk());
        }

        // --- reopen: a fresh database scans the mount from disk ---
        {
            ContentDatabase db(mount, makeFactory(), ext);

            Group* materials = db.RootGroup()->GetGroup(u8"materials");
            REQUIRE(materials != nullptr);
            REQUIRE(materials->GetInstance(u8"steel") != nullptr);

            foundation::content::Instance* byPath = db.GetInstance(u8"materials/steel");
            foundation::content::Instance* byGuid = db.GetInstance(steelId);
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
            CHECK(buffer[0] == byte{0xAB});
            CHECK(buffer[2] == byte{0xEF});

            CHECK(db.GetInstance(u8"materials/nope") == nullptr);
            CHECK(db.GetInstance(Guid{1, 2}) == nullptr);
        }

        RemoveTree(dbDir);
    }

    SerializerFactory MakeBinaryFactory() { return BinarySerializerFactory(); }
    SerializerFactory MakeXmlFactory() { return foundation::xml::XmlSerializerFactory(); }
}

RTTI_DEFINE_OBJECT(MaterialResource, "rtti::content::test")

TEST_CASE("content: binary round-trip")
{
    RunRoundTripTest(u8"scratch_content_test_db_bin", u8".rasset", MakeBinaryFactory);
}

TEST_CASE("content: XML round-trip")
{
    RunRoundTripTest(u8"scratch_content_test_db_xml", u8".xasset", MakeXmlFactory);
}

TEST_CASE("content: DeleteInstance removes envelope + stream sidecars + registrations")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    const StringView dir = u8"scratch_content_delete_db";
    RemoveTree(dir);
    NativeFileSystem mount(dir);
    ContentDatabase db(mount, BinarySerializerFactory(), u8".xasset");

    Group* materials = db.RootGroup()->CreateGroup(u8"materials");
    foundation::content::Instance* steel =
        materials->CreateInstance(u8"steel", MaterialResource::StaticType());
    REQUIRE(steel != nullptr);
    const Guid id = steel->Id();

    MaterialResource res;
    res.shininess = 3;
    REQUIRE(steel->WriteObject(res).IsOk());
    const byte extra[] = {byte{1}, byte{2}};
    REQUIRE(steel->WriteData(u8"extra", Span<const byte>(extra, 2)).IsOk());
    REQUIRE(mount.Exists(u8"materials/steel.xasset"));
    REQUIRE(mount.Exists(u8"materials/steel.extra.bin"));

    // Explicit-guid creation (the cook driver's product path): same id in another DB works.
    REQUIRE(db.DeleteInstance(id).IsOk());
    CHECK(db.GetInstance(id) == nullptr);
    CHECK(materials->GetInstance(u8"steel") == nullptr);
    CHECK_FALSE(mount.Exists(u8"materials/steel.xasset"));
    CHECK_FALSE(mount.Exists(u8"materials/steel.extra.bin"));

    // Unknown ids are a clean NotFound; a fresh explicit-id instance is registered.
    CHECK(db.DeleteInstance(id).Code() == ErrorCode::NotFound);
    foundation::content::Instance* again =
        materials->CreateInstanceWithId(id, u8"steel", MaterialResource::StaticType());
    REQUIRE(again != nullptr);
    CHECK(again->Id() == id);
    CHECK(db.GetInstance(id) == again);

    RemoveTree(dir);
}

TEST_CASE("content: Instance::DeleteData removes exactly one stream sidecar, idempotently")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    const StringView dir = u8"scratch_content_deletedata_db";
    RemoveTree(dir);
    NativeFileSystem mount(dir);
    ContentDatabase db(mount, BinarySerializerFactory(), u8".xasset");

    foundation::content::Instance* inst =
        db.RootGroup()->CreateInstance(u8"thing", MaterialResource::StaticType());
    REQUIRE(inst != nullptr);
    const byte bytes[] = {byte{7}, byte{8}};
    REQUIRE(inst->WriteData(u8"alpha", Span<const byte>(bytes, 2)).IsOk());
    REQUIRE(inst->WriteData(u8"beta", Span<const byte>(bytes, 2)).IsOk());
    REQUIRE(mount.Exists(u8"thing.alpha.bin"));
    REQUIRE(mount.Exists(u8"thing.beta.bin"));

    // Removes ONLY the named stream (instance-scoped path - no over-delete).
    CHECK(inst->DeleteData(u8"alpha").IsOk());
    CHECK_FALSE(mount.Exists(u8"thing.alpha.bin"));
    CHECK(mount.Exists(u8"thing.beta.bin"));

    // Idempotent: a missing sidecar is Ok, not an error.
    CHECK(inst->DeleteData(u8"alpha").IsOk());
    CHECK(inst->DeleteData(u8"never-written").IsOk());

    RemoveTree(dir);
}

TEST_CASE("content: CloneInstance deep-copies object + sidecars under a fresh guid")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    const StringView dir = u8"scratch_content_clone_db";
    RemoveTree(dir);
    FileDelete(JoinPath(dir, u8"materials/copper.xasset"));
    FileDelete(JoinPath(dir, u8"materials/copper.extra.bin"));
    NativeFileSystem mount(dir);
    ContentDatabase db(mount, BinarySerializerFactory(), u8".xasset");

    Group* materials = db.RootGroup()->CreateGroup(u8"materials");
    foundation::content::Instance* steel =
        materials->CreateInstance(u8"steel", MaterialResource::StaticType());
    REQUIRE(steel != nullptr);
    MaterialResource res;
    res.shininess = 7;
    res.shader = String(u8"pbr");
    REQUIRE(steel->WriteObject(res).IsOk());
    const byte extra[] = {byte{9}, byte{8}, byte{7}};
    REQUIRE(steel->WriteData(u8"extra", Span<const byte>(extra, 3)).IsOk());

    foundation::content::Instance* copy = db.CloneInstance(steel->Id(), u8"copper");
    REQUIRE(copy != nullptr);
    CHECK(copy->Id() != steel->Id()); // fresh identity
    CHECK(copy->Name() == u8"copper");
    CHECK(copy->TypeName() == steel->TypeName());
    CHECK(&copy->OwningGroup() == materials);
    CHECK(mount.Exists(u8"materials/copper.xasset"));
    CHECK(mount.Exists(u8"materials/copper.extra.bin"));

    // Deep copy: the clone's primary object matches the source's content.
    RefPtr<ISerializable> obj = copy->ReadObject();
    auto* cloned = Cast<MaterialResource>(obj.Get());
    REQUIRE(cloned != nullptr);
    CHECK(cloned->shininess == 7);
    CHECK(cloned->shader == u8"pbr");
    UniquePtr<IStream> data = copy->ReadData(u8"extra");
    REQUIRE(data);
    byte bytes[3] = {};
    REQUIRE(data->Read(bytes, 3) == 3u);
    CHECK(bytes[0] == byte{9});
    CHECK(bytes[2] == byte{7});

    // Name collisions + unknown ids fail cleanly.
    CHECK(db.CloneInstance(steel->Id(), u8"copper") == nullptr);
    Random rng(99);
    CHECK(db.CloneInstance(Guid::Generate(rng), u8"x") == nullptr);

    (void)db.DeleteInstance(copy->Id());
    (void)db.DeleteInstance(steel->Id());
    RemoveTree(dir);
}

TEST_CASE("content: RenameInstance moves envelope + sidecars; RenameGroup moves the directory")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    const StringView dir = u8"scratch_content_rename_db";
    RemoveTree(dir);
    FileDelete(JoinPath(dir, u8"materials/bronze.xasset"));
    FileDelete(JoinPath(dir, u8"materials/bronze.extra.bin"));
    FileDelete(JoinPath(dir, u8"metals/bronze.xasset"));
    FileDelete(JoinPath(dir, u8"metals/bronze.extra.bin"));
    RemoveDirectory(JoinPath(dir, u8"metals"));
    NativeFileSystem mount(dir);
    ContentDatabase db(mount, BinarySerializerFactory(), u8".xasset");

    Group* materials = db.RootGroup()->CreateGroup(u8"materials");
    foundation::content::Instance* steel =
        materials->CreateInstance(u8"steel", MaterialResource::StaticType());
    REQUIRE(steel != nullptr);
    const Guid id = steel->Id();
    MaterialResource res;
    res.shininess = 5;
    REQUIRE(steel->WriteObject(res).IsOk());
    const byte extra[] = {byte{1}, byte{2}};
    REQUIRE(steel->WriteData(u8"extra", Span<const byte>(extra, 2)).IsOk());

    // Instance rename: files move, guid + group stay, content still reads.
    REQUIRE(db.RenameInstance(id, u8"bronze").IsOk());
    CHECK(steel->Name() == u8"bronze");
    CHECK(db.GetInstance(id) == steel); // guid identity untouched
    CHECK(materials->GetInstance(u8"bronze") == steel);
    CHECK(materials->GetInstance(u8"steel") == nullptr);
    CHECK_FALSE(mount.Exists(u8"materials/steel.xasset"));
    CHECK(mount.Exists(u8"materials/bronze.xasset"));
    CHECK_FALSE(mount.Exists(u8"materials/steel.extra.bin"));
    CHECK(mount.Exists(u8"materials/bronze.extra.bin"));
    {
        RefPtr<ISerializable> object = steel->ReadObject();
        auto* loaded = Cast<MaterialResource>(object.Get());
        REQUIRE(loaded != nullptr);
        CHECK(loaded->shininess == 5);
    }

    // Collisions + bad input fail cleanly.
    foundation::content::Instance* other =
        materials->CreateInstance(u8"iron", MaterialResource::StaticType());
    REQUIRE(other != nullptr);
    CHECK(db.RenameInstance(other->Id(), u8"bronze").Code() == ErrorCode::AlreadyExists);
    CHECK(db.RenameInstance(id, u8"").Code() == ErrorCode::InvalidArgument);

    // Group rename: the directory moves; child paths derive from the new name.
    REQUIRE(db.RenameGroup(*materials, u8"metals").IsOk());
    CHECK(materials->Name() == u8"metals");
    CHECK(mount.Exists(u8"metals/bronze.xasset"));
    CHECK_FALSE(mount.Exists(u8"materials/bronze.xasset"));
    CHECK(steel->Path() == u8"metals/bronze");
    CHECK(db.RenameGroup(*db.RootGroup(), u8"x").Code() == ErrorCode::NotSupported);

    (void)db.DeleteInstance(id);
    (void)db.DeleteInstance(other->Id());
    RemoveDirectory(JoinPath(dir, u8"metals"));
    RemoveDirectory(dir);
}

TEST_CASE("content: DeleteGroup removes the whole subtree - files, directories, registrations")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    const StringView dir = u8"scratch_content_delgroup_db";
    // Explicit cleanup (RemoveTree only knows the shared fixture paths).
    auto scrub = [&]()
    {
        FileDelete(JoinPath(dir, u8"outer/inner/b.xasset"));
        FileDelete(JoinPath(dir, u8"outer/a.xasset"));
        FileDelete(JoinPath(dir, u8"outer/a.extra.bin"));
        FileDelete(JoinPath(dir, u8"keep.xasset"));
        RemoveDirectory(JoinPath(dir, u8"outer/inner"));
        RemoveDirectory(JoinPath(dir, u8"outer"));
        RemoveDirectory(dir);
    };
    scrub();
    {
        NativeFileSystem mount(dir);
        ContentDatabase db(mount, BinarySerializerFactory(), u8".xasset");

        // outer/ { a (+sidecar), inner/ { b } } and an unrelated sibling instance.
        Group* outer = db.RootGroup()->CreateGroup(u8"outer");
        Group* inner = outer->CreateGroup(u8"inner");
        foundation::content::Instance* a =
            outer->CreateInstance(u8"a", MaterialResource::StaticType());
        foundation::content::Instance* b =
            inner->CreateInstance(u8"b", MaterialResource::StaticType());
        foundation::content::Instance* keep =
            db.RootGroup()->CreateInstance(u8"keep", MaterialResource::StaticType());
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(keep != nullptr);
        MaterialResource res;
        REQUIRE(a->WriteObject(res).IsOk());
        REQUIRE(b->WriteObject(res).IsOk());
        REQUIRE(keep->WriteObject(res).IsOk());
        const byte extra[] = {byte{7}};
        REQUIRE(a->WriteData(u8"extra", Span<const byte>(extra, 1)).IsOk());
        const Guid aId = a->Id();
        const Guid bId = b->Id();
        const Guid keepId = keep->Id();

        // The root refuses.
        CHECK(db.DeleteGroup(*db.RootGroup()).Code() == ErrorCode::NotSupported);

        REQUIRE(db.DeleteGroup(*outer).IsOk()); // outer/inner/a/b are DANGLING after this

        // Registrations gone (guid index + tree), the sibling untouched.
        CHECK(db.GetInstance(aId) == nullptr);
        CHECK(db.GetInstance(bId) == nullptr);
        CHECK(db.RootGroup()->GetGroup(u8"outer") == nullptr);
        CHECK(db.GetInstance(keepId) == keep);

        // Files AND directories gone (a rescan must not resurrect ghost groups).
        CHECK_FALSE(mount.Exists(u8"outer/a.xasset"));
        CHECK_FALSE(mount.Exists(u8"outer/a.extra.bin"));
        CHECK_FALSE(mount.Exists(u8"outer/inner/b.xasset"));
        CHECK_FALSE(mount.Exists(u8"outer/inner"));
        CHECK_FALSE(mount.Exists(u8"outer"));
        CHECK(mount.Exists(u8"keep.xasset"));
    }
    {
        // Rescan proves it: a fresh database over the same mount has no ghost of the group.
        NativeFileSystem mount(dir);
        ContentDatabase db(mount, BinarySerializerFactory(), u8".xasset");
        CHECK(db.RootGroup()->GetGroup(u8"outer") == nullptr);
        CHECK(db.RootGroup()->GetInstance(u8"keep") != nullptr);
    }
    scrub();
}

TEST_CASE("content: instance guids are unique across database sessions")
{
    // Regression: the guid rng was default-seeded (fixed PCG constant), so every session
    // replayed the SAME guid sequence - a delete + reimport in a fresh session gave old
    // guids to different assets, cross-typing persisted cook records/products (crash).
    const StringView dirA = u8"scratch_content_guid_a";
    const StringView dirB = u8"scratch_content_guid_b";
    RemoveTree(dirA);
    RemoveTree(dirB);
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    NativeFileSystem mountA(dirA);
    NativeFileSystem mountB(dirB);
    ContentDatabase a(mountA, BinarySerializerFactory(), u8".rasset");
    ContentDatabase b(mountB, BinarySerializerFactory(), u8".rasset");

    // Two fresh databases minting the same creation sequence must NOT agree on guids.
    Array<Guid> fromA;
    Array<Guid> fromB;
    Group* ga = a.RootGroup()->CreateGroup(u8"materials");
    Group* gb = b.RootGroup()->CreateGroup(u8"materials");
    for (int i = 0; i < 4; ++i)
    {
        String name(u8"m");
        name.PushBack(static_cast<char8_t>(u8'0' + i));
        fromA.PushBack(ga->CreateInstance(name.AsView(), MaterialResource::StaticType())->Id());
        fromB.PushBack(gb->CreateInstance(name.AsView(), MaterialResource::StaticType())->Id());
    }
    usize collisions = 0;
    for (const Guid& x : fromA)
    {
        for (const Guid& y : fromB)
        {
            if (x == y)
            {
                ++collisions;
            }
        }
    }
    CHECK(collisions == 0u);

    RemoveTree(dirA);
    RemoveTree(dirB);
}

TEST_CASE("content: UniqueInstanceName / UniqueGroupName - the one general dedup")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    const StringView dir = u8"scratch_content_unique_name_db";
    RemoveTree(dir);
    NativeFileSystem mount(dir);
    ContentDatabase db(mount, BinarySerializerFactory(), u8".rasset");
    Group* group = db.RootGroup()->CreateGroup(u8"materials");
    REQUIRE(group != nullptr);

    // A free base comes back untouched.
    CHECK(group->UniqueInstanceName(u8"Thing") == u8"Thing");

    // A taken base steps to `base.2`, `base.3`, ... (CreateInstance would silently
    // return the EXISTING instance - the overwrite hazard the helper exists for).
    foundation::content::Instance* first =
        group->CreateInstance(u8"Thing", MaterialResource::StaticType());
    REQUIRE(first != nullptr);
    CHECK(group->CreateInstance(u8"Thing", MaterialResource::StaticType()) == first);
    CHECK(group->UniqueInstanceName(u8"Thing") == u8"Thing.2");

    // Digits emit most-significant first - counter 12 is "Thing.12", never "Thing.21"
    // (the bug every hand-rolled copy of this loop had).
    for (i32 counter = 2; counter <= 11; ++counter)
    {
        const String next = group->UniqueInstanceName(u8"Thing");
        REQUIRE(group->CreateInstance(next.AsView(), MaterialResource::StaticType()) != nullptr);
    }
    CHECK(group->UniqueInstanceName(u8"Thing") == u8"Thing.12");

    // Same convention for child groups.
    CHECK(group->UniqueGroupName(u8"sub") == u8"sub");
    REQUIRE(group->CreateGroup(u8"sub") != nullptr);
    CHECK(group->UniqueGroupName(u8"sub") == u8"sub.2");

    RemoveTree(dir);
}

TEST_CASE("content: the open scan reports envelope count + bytes opened (I4b instrumentation)")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    const StringView dir = u8"scratch_content_scanstats";
    // The suite's RemoveTree only knows the round-trip test's fixed paths; this test writes
    // its own instances, so clean recursively or the previous run's envelopes pollute the
    // construction scan (otherwise envelopes==3 on the authoring db).
    (void)RemoveDirectoryRecursive(dir);
    NativeFileSystem mount(dir);
    {
        ContentDatabase db(mount, foundation::xml::XmlSerializerFactory(), u8".xasset");
        Group* group = db.RootGroup()->CreateGroup(u8"assets");
        for (i32 i = 0; i < 3; ++i)
        {
            String name = Format(u8"m{}", i);
            foundation::content::Instance* inst =
                group->CreateInstance(name.AsView(), MaterialResource::StaticType());
            REQUIRE(inst != nullptr);
            MaterialResource mat;
            mat.shininess = i;
            CHECK(inst->WriteObject(mat).IsOk());
        }
        // The authoring db never scanned (built in memory): zero envelopes opened.
        CHECK(db.LastScanStats().envelopes == 0);
    }
    {
        ContentDatabase db(mount, foundation::xml::XmlSerializerFactory(), u8".xasset");
        // The reopen scan touched all three envelopes and counted their full file sizes -
        // the number that prices the open-time header scan (XML DOM-parses whole files).
        CHECK(db.LastScanStats().envelopes == 3);
        CHECK(db.LastScanStats().bytesOpened > 0);
    }
}

TEST_CASE("content: group children stay name-sorted through create and rename")
{
    GlobalTypeRegistry().Register(MaterialResource::StaticType());
    RegisterSerializable<MaterialResource>();

    const StringView dir = u8"scratch_content_sort_db";
    RemoveDirectory(JoinPath(dir, u8"zinc"));
    RemoveDirectory(JoinPath(dir, u8"Alpha"));
    RemoveDirectory(JoinPath(dir, u8"metals"));
    RemoveDirectory(JoinPath(dir, u8"steel"));
    RemoveDirectory(dir);
    NativeFileSystem mount(dir);
    ContentDatabase db(mount, BinarySerializerFactory(), u8".xasset");

    // Created deliberately out of order (and mixed case) - readdir/import order must not
    // leak into the surfaced order.
    Group* root = db.RootGroup();
    (void)root->CreateGroup(u8"zinc");
    (void)root->CreateGroup(u8"Alpha");
    (void)root->CreateGroup(u8"metals");
    REQUIRE(root->Groups().Size() == 3u);
    CHECK(root->Groups()[0]->Name() == u8"Alpha");
    CHECK(root->Groups()[1]->Name() == u8"metals");
    CHECK(root->Groups()[2]->Name() == u8"zinc");

    Group* metals = root->GetGroup(u8"metals");
    (void)metals->CreateInstance(u8"tin", MaterialResource::StaticType());
    (void)metals->CreateInstance(u8"Bronze", MaterialResource::StaticType());
    foundation::content::Instance* steel =
        metals->CreateInstance(u8"steel", MaterialResource::StaticType());
    REQUIRE(metals->Instances().Size() == 3u);
    CHECK(metals->Instances()[0]->Name() == u8"Bronze");
    CHECK(metals->Instances()[1]->Name() == u8"steel");
    CHECK(metals->Instances()[2]->Name() == u8"tin");

    // A rename re-sorts its siblings.
    REQUIRE(db.RenameInstance(steel->Id(), u8"zeta").IsOk());
    CHECK(metals->Instances()[2]->Name() == u8"zeta");
    Group* alpha = root->GetGroup(u8"Alpha");
    REQUIRE(db.RenameGroup(*alpha, u8"omega").IsOk());
    CHECK(root->Groups()[0]->Name() == u8"metals");
    CHECK(root->Groups()[1]->Name() == u8"omega");
    CHECK(root->Groups()[2]->Name() == u8"zinc");

    RemoveDirectory(JoinPath(dir, u8"metals"));
    RemoveDirectory(dir);
}
