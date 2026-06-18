#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <cstring>

import raptor.core;
import raptor.vfs;
import raptor.vfs.pak;

using namespace raptor::core;
using namespace raptor::vfs;

namespace
{
    Span<const byte> Bytes(const char* s)
    {
        return Span<const byte>{ reinterpret_cast<const byte*>(s), std::strlen(s) };
    }
}

TEST_CASE("vfs.pak: build, open, read, enumerate")
{
    const StringView pak = u"raptor_pak_test.pak";
    FileDelete(pak);

    // --- build ---
    {
        PakBuilder builder;
        builder.Add(u"hello.txt", Bytes("hello world"));
        builder.Add(u"data/blob.bin", Bytes("XYZ"));
        builder.Add(u"data/deep/leaf.txt", Bytes("leaf"));
        REQUIRE(builder.Write(pak).IsOk());
    }

    // --- open ---
    PakFileSystem fs(pak);
    REQUIRE(fs.IsValid());
    CHECK(fs.EntryCount() == 3u);

    // Capabilities: read + enumerate, but not writable.
    CHECK(fs.AsEnumerable() != nullptr);
    CHECK(fs.AsWritable() == nullptr);

    // Existence.
    CHECK(fs.Exists(u"hello.txt"));
    CHECK(fs.Exists(u"data/blob.bin"));
    CHECK_FALSE(fs.Exists(u"missing.txt"));

    // Read an entry back.
    {
        UniquePtr<IStream> stream = fs.Open(u"hello.txt", FileMode::Read);
        REQUIRE(static_cast<bool>(stream));
        char buffer[16] = {};
        const u64 n = stream->Read(buffer, 11);
        CHECK(n == 11u);
        CHECK(std::strncmp(buffer, "hello world", 11) == 0);
    }

    // Write mode is unsupported (read-only archive).
    CHECK_FALSE(static_cast<bool>(fs.Open(u"hello.txt", FileMode::Write)));
    // Missing entry.
    CHECK_FALSE(static_cast<bool>(fs.Open(u"nope", FileMode::Read)));

    // Enumerate root: a file (hello.txt) and a directory (data/).
    {
        Array<DirEntry> entries;
        REQUIRE(fs.Enumerate(u"", entries).IsOk());
        bool foundFile = false;
        bool foundDir = false;
        for (const DirEntry& e : entries)
        {
            if (e.name == u"hello.txt") { foundFile = true; CHECK_FALSE(e.isDirectory); }
            if (e.name == u"data") { foundDir = true; CHECK(e.isDirectory); }
        }
        CHECK(foundFile);
        CHECK(foundDir);
    }

    // Enumerate a subfolder: a file (blob.bin) and a nested directory (deep/).
    {
        Array<DirEntry> entries;
        REQUIRE(fs.Enumerate(u"data", entries).IsOk());
        bool foundBlob = false;
        bool foundDeep = false;
        for (const DirEntry& e : entries)
        {
            if (e.name == u"blob.bin") { foundBlob = true; CHECK_FALSE(e.isDirectory); }
            if (e.name == u"deep") { foundDeep = true; CHECK(e.isDirectory); }
        }
        CHECK(foundBlob);
        CHECK(foundDeep);
    }

    FileDelete(pak);
}

TEST_CASE("vfs.pak: a non-pak file is rejected")
{
    const StringView path = u"raptor_pak_bad.pak";
    const byte junk[] = { byte{ 1 }, byte{ 2 }, byte{ 3 }, byte{ 4 } };
    REQUIRE(WriteFile(path, Span<const byte>{ junk, ArrayCount(junk) }).IsOk());

    PakFileSystem fs(path);
    CHECK_FALSE(fs.IsValid());
    CHECK_FALSE(fs.Exists(u"anything"));

    FileDelete(path);
}
