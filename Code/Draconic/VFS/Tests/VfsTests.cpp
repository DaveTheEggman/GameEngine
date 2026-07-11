#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.vfs;

using namespace draconic::core;
using namespace draconic::vfs;

TEST_CASE("vfs: NativeFileSystem read + scheme-routed VirtualFileSystem")
{
    const StringView file = u8"draconic_vfs_test.tmp";
    const byte data[] = { byte{ 7 }, byte{ 8 }, byte{ 9 } };
    REQUIRE(WriteFile(file, Span<const byte>{ data, ArrayCount(data) }).IsOk());

    NativeFileSystem native(u8".");
    CHECK(native.Exists(u8"draconic_vfs_test.tmp"));
    CHECK_FALSE(native.Exists(u8"draconic_vfs_nope.xyz"));
    {
        UniquePtr<IStream> stream = native.Open(u8"draconic_vfs_test.tmp", FileMode::Read);
        REQUIRE(static_cast<bool>(stream));
        byte buffer[3] = {};
        CHECK(stream->Read(buffer, 3) == 3u);
        CHECK(buffer[0] == byte{ 7 });
        CHECK(buffer[2] == byte{ 9 });
    }

    // Mount under a scheme; address as "assets://...".
    VirtualFileSystem vfs;
    vfs.Mount(u8"assets", native);
    CHECK(vfs.GetMount(u8"assets") == &native);
    CHECK(vfs.GetMount(u8"missing") == nullptr);
    CHECK(vfs.Exists(u8"assets://draconic_vfs_test.tmp"));
    CHECK_FALSE(vfs.Exists(u8"assets://nope.xyz"));
    CHECK_FALSE(vfs.Exists(u8"unmounted://whatever"));
    CHECK_FALSE(vfs.Exists(u8"schemeless/path"));   // no scheme -> rejected
    {
        UniquePtr<IStream> stream = vfs.Open(u8"assets://draconic_vfs_test.tmp", FileMode::Read);
        REQUIRE(static_cast<bool>(stream));
        byte b = byte{ 0 };
        CHECK(stream->Read(&b, 1) == 1u);
        CHECK(b == byte{ 7 });
    }
    CHECK_FALSE(static_cast<bool>(vfs.Open(u8"unmounted://x", FileMode::Read)));

    CHECK(FileDelete(file));
}

TEST_CASE("vfs: capability queries via As*()")
{
    NativeFileSystem native(u8".");
    IFileSystem& fs = native;

    REQUIRE(fs.AsEnumerable() != nullptr);
    REQUIRE(fs.AsWritable() != nullptr);
    REQUIRE(fs.AsStat() != nullptr);
    CHECK(fs.AsWatchable() == nullptr);   // lands with the 6d watcher pass

    // A router advertises no capabilities of its own.
    VirtualFileSystem vfs;
    CHECK(vfs.AsEnumerable() == nullptr);
    CHECK(vfs.AsWritable() == nullptr);
}

TEST_CASE("vfs: writable + enumerable round-trip")
{
    NativeFileSystem native(u8".");
    IWritableFileSystem* w = native.AsWritable();
    IEnumerableFileSystem* e = native.AsEnumerable();
    REQUIRE(w != nullptr);
    REQUIRE(e != nullptr);

    // Save creates intermediate directories.
    const byte payload[] = { byte{ 1 }, byte{ 2 }, byte{ 3 }, byte{ 4 } };
    REQUIRE(w->Save(u8"draconic_vfs_dir/sub/blob.bin", Span<const byte>{ payload, ArrayCount(payload) }).IsOk());
    CHECK(native.Exists(u8"draconic_vfs_dir/sub/blob.bin"));

    // Enumerate the subfolder; the blob is listed and is not a directory.
    Array<DirEntry> entries;
    REQUIRE(e->Enumerate(u8"draconic_vfs_dir/sub", entries).IsOk());
    bool foundBlob = false;
    for (const DirEntry& entry : entries)
    {
        if (entry.name == u8"blob.bin") { foundBlob = true; CHECK_FALSE(entry.isDirectory); }
    }
    CHECK(foundBlob);

    // Enumerate the parent; "sub" is listed as a directory.
    Array<DirEntry> parent;
    REQUIRE(e->Enumerate(u8"draconic_vfs_dir", parent).IsOk());
    bool foundSub = false;
    for (const DirEntry& entry : parent)
    {
        if (entry.name == u8"sub") { foundSub = true; CHECK(entry.isDirectory); }
    }
    CHECK(foundSub);

    // Enumerate of a missing folder fails.
    Array<DirEntry> missing;
    CHECK_FALSE(e->Enumerate(u8"draconic_vfs_dir/nope", missing).IsOk());

    // Cleanup.
    CHECK(w->Delete(u8"draconic_vfs_dir/sub/blob.bin").IsOk());
    CHECK(RemoveDirectory(u8"draconic_vfs_dir/sub"));
    CHECK(RemoveDirectory(u8"draconic_vfs_dir"));
}

TEST_CASE("vfs: NativeFileSystem stat reports size + modified time")
{
    NativeFileSystem native(u8".");
    IWritableFileSystem* w = native.AsWritable();
    IStatFileSystem* st = native.AsStat();
    REQUIRE(w != nullptr);
    REQUIRE(st != nullptr);

    const byte payload[5] = { byte{1}, byte{2}, byte{3}, byte{4}, byte{5} };
    REQUIRE(w->Save(u8"vfs_stat_test.bin", Span<const byte>(payload, 5)).IsOk());

    FileStatInfo info;
    REQUIRE(st->Stat(u8"vfs_stat_test.bin", info));
    CHECK(info.size == 5u);
    CHECK(info.modifiedTime > 0);   // a plausible wall-clock epoch time

    // Rewriting changes the size; mtime moves monotonically (>=, same-second writes allowed).
    const i64 firstTime = info.modifiedTime;
    REQUIRE(w->Save(u8"vfs_stat_test.bin", Span<const byte>(payload, 3)).IsOk());
    REQUIRE(st->Stat(u8"vfs_stat_test.bin", info));
    CHECK(info.size == 3u);
    CHECK(info.modifiedTime >= firstTime);

    // Missing files and directories are not regular files.
    CHECK_FALSE(st->Stat(u8"vfs_stat_missing.bin", info));
    CHECK_FALSE(st->Stat(u8"", info));

    REQUIRE(w->Delete(u8"vfs_stat_test.bin").IsOk());
}
