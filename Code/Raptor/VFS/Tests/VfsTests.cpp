#include <doctest/doctest.h>

#include "Core/Prelude.h"

import raptor.core;
import raptor.vfs;

using namespace raptor::core;
using namespace raptor::vfs;

TEST_CASE("vfs: NativeFileSystem and VirtualFileSystem mounting")
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

    // Mount the native FS under a logical prefix.
    VirtualFileSystem vfs;
    vfs.Mount(u"assets", native);
    CHECK(vfs.Exists(u"assets/raptor_vfs_test.tmp"));
    CHECK_FALSE(vfs.Exists(u"unmounted/whatever"));
    {
        UniquePtr<IStream> stream = vfs.Open(u"assets/raptor_vfs_test.tmp", FileMode::Read);
        REQUIRE(static_cast<bool>(stream));
        byte b = byte{ 0 };
        CHECK(stream->Read(&b, 1) == 1u);
        CHECK(b == byte{ 7 });
    }
    // Unmounted prefix -> no stream.
    CHECK_FALSE(static_cast<bool>(vfs.Open(u"unmounted/x", FileMode::Read)));

    CHECK(FileDelete(file));
}
