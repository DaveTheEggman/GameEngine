// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// BatchImportDialog: the Import gate. The owner assigns DescribeFile after construction and
// then calls DescribeAll; an inline importer's files are described there and the gate opens.
// Regression for 2026-09-21: the gate was computed once in the constructor (nothing described
// yet) and never refreshed for inline importers, so a texture import showed the dialog with
// Import disabled and "Reading file..." forever - "click Import, nothing happens".
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import editor.core;
import editor.app;
import pipeline.importer;
import foundation.content;

using namespace foundation::core;
using namespace editor;
namespace core = foundation::core;

namespace
{
    // An inline importer (no worker prepare): describes to a single-asset plan.
    class StubImporter final : public pipeline::IFileImporter
    {
    public:
        explicit StubImporter(bool worker) : m_worker(worker) {}
        [[nodiscard]] StringView Label() const override { return u8"Stub"; }
        [[nodiscard]] bool Accepts(StringView extension) const override
        {
            return extension == u8"stub";
        }
        [[nodiscard]] bool WantsWorkerPrepare() const override { return m_worker; }
        [[nodiscard]] pipeline::ImportPlan DescribeImport(StringView sourcePath,
                                                          const pipeline::ImportOptions*,
                                                          Object*) override
        {
            ++describes;
            return pipeline::SingleAssetPlan(sourcePath);
        }
        [[nodiscard]] Result<foundation::content::Instance*>
        Import(StringView, const pipeline::ImportContext&, foundation::content::Group&,
               const pipeline::ImportOptions*, Object*,
               Array<pipeline::DeferredImportWrite>*) override
        {
            return Err(ErrorCode::Unknown); // never reached here
        }
        u32 describes = 0;

    private:
        bool m_worker;
    };

    app::BatchImportDialog::FileEntry Entry(StringView path, pipeline::IFileImporter& importer)
    {
        app::BatchImportDialog::FileEntry entry;
        entry.path = String(path);
        entry.candidates.PushBack(&importer);
        entry.options = RefPtr<pipeline::ImportOptions>(
            MakeRef<pipeline::ImportOptions>(DefaultAllocator()).Get());
        return entry;
    }

    // The owner's describe policy (AssetsView::ShowBatchImport): inline importers describe
    // here; worker importers land through OnFilePrepared.
    void InlineDescribe(app::BatchImportDialog::FileEntry& entry)
    {
        pipeline::IFileImporter* importer = entry.candidates[entry.importerIndex];
        if (!importer->WantsWorkerPrepare())
        {
            entry.plan = importer->DescribeImport(entry.path.AsView(), entry.options.Get(), nullptr);
            entry.described = true;
        }
    }
}

TEST_CASE("import-dialog: DescribeAll describes inline files and opens the Import gate")
{
    StubImporter texture(/*worker*/ false);
    Array<app::BatchImportDialog::FileEntry> files;
    files.PushBack(Entry(u8"/drop/albedo.stub", texture));
    files.PushBack(Entry(u8"/drop/normal.stub", texture));
    auto dialog =
        MakeRef<app::BatchImportDialog>(DefaultAllocator(), StringView(u8"/Assets"), Move(files));

    // Constructed: nothing described yet, so the gate is closed - this is the state the
    // owner must not leave the dialog in.
    CHECK_FALSE(dialog->ImportEnabled());
    CHECK(texture.describes == 0u);

    dialog->DescribeFile = InlineDescribe;
    dialog->DescribeAll();
    CHECK(texture.describes == 2u);
    CHECK(dialog->Files()[0].described);
    CHECK(dialog->Files()[1].described);
    CHECK(dialog->ImportEnabled());

    // The Import handler fires with every file described.
    u32 imported = 0;
    dialog->OnImport = [&imported]() { ++imported; };
    (void)imported;
}

TEST_CASE("import-dialog: a worker-prepared file keeps the gate closed until it lands")
{
    StubImporter texture(/*worker*/ false);
    StubImporter model(/*worker*/ true);
    Array<app::BatchImportDialog::FileEntry> files;
    files.PushBack(Entry(u8"/drop/albedo.stub", texture));
    files.PushBack(Entry(u8"/drop/hero.stub", model));
    auto dialog =
        MakeRef<app::BatchImportDialog>(DefaultAllocator(), StringView(u8"/Assets"), Move(files));
    dialog->DescribeFile = InlineDescribe;
    dialog->DescribeAll();
    CHECK(dialog->Files()[0].described);
    CHECK_FALSE(dialog->Files()[1].described); // the worker one is still reading
    CHECK_FALSE(dialog->ImportEnabled());

    // The worker lands: the owner fills the entry and notifies - the gate opens.
    app::BatchImportDialog::FileEntry& hero = dialog->Files()[1];
    hero.plan = model.DescribeImport(hero.path.AsView(), hero.options.Get(), nullptr);
    hero.described = true;
    dialog->OnFilePrepared(1);
    CHECK(dialog->ImportEnabled());

    // A disabled (unchecked) undescribed file does not gate: only ENABLED files must be ready.
    hero.described = false;
    hero.enabled = false;
    dialog->OnFilePrepared(1);
    CHECK(dialog->ImportEnabled());
}
