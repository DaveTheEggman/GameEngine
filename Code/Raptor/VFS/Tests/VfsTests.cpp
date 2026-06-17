#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.vfs;

using namespace raptor::core;
using namespace raptor::vfs;

TEST_CASE("vfs: NativeFileSystem read + scheme-routed VirtualFileSystem")
{
    const StringView file = u"raptor_vfs_test.tmp";
    const byte data[] = { byte{ 7 }, byte{ 8 }, byte{ 9 } };
    REQUIRE(WriteFile(file, Span<const byte>{ data, ArrayCount(data) }).IsOk());

    NativeFileSystem native(u".");
    CHECK(native.Exists(u"raptor_vfs_test.tmp"));
    CHECK_FALSE(native.Exists(u"raptor_vfs_nope.xyz"));
    {
        UniquePtr<IStream> stream = native.Open(u"raptor_vfs_test.tmp", FileMode::Read);
        REQUIRE(static_cast<bool>(stream));
        byte buffer[3] = {};
        CHECK(stream->Read(buffer, 3) == 3u);
        CHECK(buffer[0] == byte{ 7 });
        CHECK(buffer[2] == byte{ 9 });
    }

    // Mount under a scheme; address as "assets://...".
    VirtualFileSystem vfs;
    vfs.Mount(u"assets", native);
    CHECK(vfs.GetMount(u"assets") == &native);
    CHECK(vfs.GetMount(u"missing") == nullptr);
    CHECK(vfs.Exists(u"assets://raptor_vfs_test.tmp"));
    CHECK_FALSE(vfs.Exists(u"assets://nope.xyz"));
    CHECK_FALSE(vfs.Exists(u"unmounted://whatever"));
    CHECK_FALSE(vfs.Exists(u"schemeless/path"));   // no scheme -> rejected
    {
        UniquePtr<IStream> stream = vfs.Open(u"assets://raptor_vfs_test.tmp", FileMode::Read);
        REQUIRE(static_cast<bool>(stream));
        byte b = byte{ 0 };
        CHECK(stream->Read(&b, 1) == 1u);
        CHECK(b == byte{ 7 });
    }
    CHECK_FALSE(static_cast<bool>(vfs.Open(u"unmounted://x", FileMode::Read)));

    CHECK(FileDelete(file));
}

TEST_CASE("vfs: capability queries via As*()")
{
    NativeFileSystem native(u".");
    IFileSystem& fs = native;

    REQUIRE(fs.AsEnumerable() != nullptr);
    REQUIRE(fs.AsWritable() != nullptr);
    CHECK(fs.AsWatchable() == nullptr);   // not implemented yet

    // A router advertises no capabilities of its own.
    VirtualFileSystem vfs;
    CHECK(vfs.AsEnumerable() == nullptr);
    CHECK(vfs.AsWritable() == nullptr);
}

TEST_CASE("vfs: writable + enumerable round-trip")
{
    NativeFileSystem native(u".");
    IWritableFileSystem* w = native.AsWritable();
    IEnumerableFileSystem* e = native.AsEnumerable();
    REQUIRE(w != nullptr);
    REQUIRE(e != nullptr);

    // Save creates intermediate directories.
    const byte payload[] = { byte{ 1 }, byte{ 2 }, byte{ 3 }, byte{ 4 } };
    REQUIRE(w->Save(u"raptor_vfs_dir/sub/blob.bin", Span<const byte>{ payload, ArrayCount(payload) }).IsOk());
    CHECK(native.Exists(u"raptor_vfs_dir/sub/blob.bin"));

    // Enumerate the subfolder; the blob is listed and is not a directory.
    Array<DirEntry> entries;
    REQUIRE(e->Enumerate(u"raptor_vfs_dir/sub", entries).IsOk());
    bool foundBlob = false;
    for (const DirEntry& entry : entries)
    {
        if (entry.name == u"blob.bin") { foundBlob = true; CHECK_FALSE(entry.isDirectory); }
    }
    CHECK(foundBlob);

    // Enumerate the parent; "sub" is listed as a directory.
    Array<DirEntry> parent;
    REQUIRE(e->Enumerate(u"raptor_vfs_dir", parent).IsOk());
    bool foundSub = false;
    for (const DirEntry& entry : parent)
    {
        if (entry.name == u"sub") { foundSub = true; CHECK(entry.isDirectory); }
    }
    CHECK(foundSub);

    // Enumerate of a missing folder fails.
    Array<DirEntry> missing;
    CHECK_FALSE(e->Enumerate(u"raptor_vfs_dir/nope", missing).IsOk());

    // Cleanup.
    CHECK(w->Delete(u"raptor_vfs_dir/sub/blob.bin").IsOk());
    CHECK(RemoveDirectory(u"raptor_vfs_dir/sub"));
    CHECK(RemoveDirectory(u"raptor_vfs_dir"));
}
