// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Importer registry + shared import helpers: extension routing, path helpers, and the
// copy-into-Sources step every file importer builds on.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <initializer_list>

import foundation.core;
import foundation.content;
import pipeline.importer;

using namespace foundation::core;
using namespace pipeline;

namespace
{
    class FakeImporter final : public IFileImporter
    {
    public:
        [[nodiscard]] StringView Label() const override { return u8"Fake"; }
        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            return extension == StringView(u8"fak");
        }
        [[nodiscard]] Result<foundation::content::Instance*>
        Import(StringView, const ImportContext&, foundation::content::Group&,
               const pipeline::ImportOptions*, Object*,
               Array<DeferredImportWrite>*) override
        {
            return Err(ErrorCode::NotSupported);
        }
    };

    // A second importer that ALSO claims "fak" - exercises the multi-match chooser path (FindAllFor).
    class FakeImporter2 final : public IFileImporter
    {
    public:
        [[nodiscard]] StringView Label() const override { return u8"Fake2"; }
        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            return extension == StringView(u8"fak");
        }
        [[nodiscard]] Result<foundation::content::Instance*>
        Import(StringView, const ImportContext&, foundation::content::Group&,
               const pipeline::ImportOptions*, Object*, Array<DeferredImportWrite>*) override
        {
            return Err(ErrorCode::NotSupported);
        }
    };
}

TEST_CASE("importer: path helpers")
{
    CHECK(FileExtensionLower(u8"/a/b/Foo.PNG") == StringView(u8"png"));
    CHECK(FileExtensionLower(u8"C:\\art\\thing.Jpeg") == StringView(u8"jpeg"));
    CHECK(FileExtensionLower(u8"noext") == StringView(u8""));
    CHECK(FileExtensionLower(u8"/dotted.dir/noext") == StringView(u8""));

    CHECK(FileNameOf(u8"/a/b/foo.png") == StringView(u8"foo.png"));
    CHECK(FileNameOf(u8"C:\\a\\b.png") == StringView(u8"b.png"));
    CHECK(FileNameOf(u8"bare.png") == StringView(u8"bare.png"));

    CHECK(FileStemOf(u8"foo.png") == StringView(u8"foo"));
    CHECK(FileStemOf(u8"foo") == StringView(u8"foo"));
}

TEST_CASE("importer: registry routes by extension, first match wins")
{
    ImporterRegistry registry;
    CHECK(registry.FindFor(u8"fak") == nullptr);

    registry.Register(
        UniquePtr<IFileImporter>(DefaultAllocator().New<FakeImporter>(), DefaultAllocator()));
    CHECK(registry.Count() == 1u);

    IFileImporter* importer = registry.FindFor(u8"fak");
    REQUIRE(importer != nullptr);
    CHECK(importer->Label() == StringView(u8"Fake"));
    CHECK(registry.FindFor(u8"png") == nullptr);
}

TEST_CASE("importer: FindAllFor returns every match (registration order) for the chooser")
{
    ImporterRegistry registry;
    CHECK(registry.FindAllFor(u8"fak").IsEmpty());

    registry.Register(
        UniquePtr<IFileImporter>(DefaultAllocator().New<FakeImporter>(), DefaultAllocator()));
    registry.Register(
        UniquePtr<IFileImporter>(DefaultAllocator().New<FakeImporter2>(), DefaultAllocator()));

    Array<IFileImporter*> matches = registry.FindAllFor(u8"fak");
    REQUIRE(matches.Size() == 2u);
    CHECK(matches[0]->Label() == StringView(u8"Fake")); // registration order preserved
    CHECK(matches[1]->Label() == StringView(u8"Fake2"));
    CHECK(registry.FindAllFor(u8"png").IsEmpty());
}

TEST_CASE("importer: CopyIntoSources lands the bytes in the project's Sources tree")
{
    const StringView dir = u8"scratch_importer_test_project";
    // Clean slate.
    FileDelete(PathJoin(dir, u8"Project.xml"));
    FileDelete(PathJoin(dir, u8"Sources/payload.bin"));
    for (StringView sub : {u8"Content", u8"Sources", u8"Cooked", u8"Editor", u8".cache"})
    {
        RemoveDirectory(PathJoin(dir, sub));
    }
    RemoveDirectory(dir);

    // Project-free: CopyIntoSources needs only a sources DIRECTORY - the pipeline is
    // headless-drivable by design, so the test provides a bare scratch tree, no editor.
    const String sourcesRoot = PathJoin(dir, u8"Sources");
    REQUIRE(CreateDirectories(sourcesRoot.AsView()));

    // A loose OS file to import.
    const byte payload[4] = {byte{9}, byte{8}, byte{7}, byte{6}};
    REQUIRE(WriteFile(u8"scratch_importer_loose.bin", Span<const byte>(payload, 4)).IsOk());

    Result<String> name = CopyIntoSources(ImportContext{String(sourcesRoot.AsView())}, u8"scratch_importer_loose.bin");
    REQUIRE(name.HasValue());
    CHECK(name.Value() == StringView(u8"scratch_importer_loose.bin"));

    const String copied = PathJoin(sourcesRoot.AsView(), name.Value().AsView());
    Result<Array<byte>> bytes = ReadFile(copied.AsView());
    REQUIRE(bytes.HasValue());
    CHECK(bytes.Value().Size() == 4u);
    CHECK(bytes.Value()[0] == byte{9});

    // Reimporting the same name reuses the existing copy (no error).
    CHECK(CopyIntoSources(ImportContext{String(sourcesRoot.AsView())}, u8"scratch_importer_loose.bin").HasValue());
    // Missing source is a clean failure.
    CHECK_FALSE(CopyIntoSources(ImportContext{String(sourcesRoot.AsView())}, u8"scratch_importer_missing.bin").HasValue());

    FileDelete(u8"scratch_importer_loose.bin");
    FileDelete(copied.AsView());
    FileDelete(PathJoin(dir, u8"Project.xml"));
    for (StringView sub : {u8"Content", u8"Sources", u8"Cooked", u8"Editor", u8".cache"})
    {
        RemoveDirectory(PathJoin(dir, sub));
    }
    RemoveDirectory(dir);
}
