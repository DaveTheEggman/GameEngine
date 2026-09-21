// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :import_dialog partition.
//
// The pre-import review dialog (Sedulous ImportDialog lineage): dropping a file on the asset
// browser pops this up when the routed importer has options. Shows the source file, the
// destination group, the importer's RESOURCE PLAN when it provides one (DescribeImport - a
// scrollable per-kind list with a checkbox + editable target name per resource, and a
// check-all per section), and one checkbox per ImportOptions::Toggle, then Import/Cancel.
// TakePlan() hands the edited plan back to the caller, who stores it as options.selection so
// the importer's fan-out skips deselected resources and honors renames.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:import_dialog;

import foundation.core;
import foundation.ui;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;

    /// The per-kind resource list of an import plan: a check-all header per kind, then one
    /// row per resource (enable checkbox + target-name editor). Edits an EXTERNAL plan in
    /// place (checkboxes live; call SyncNames before reading the plan - the batch dialog
    /// switches files without tearing this down at commit time). The plan (and its entries'
    /// addresses) must outlive the view.
    class ImportPlanView final : public ui::FlexLayout
    {
        RTTI_OBJECT(ImportPlanView, ui::FlexLayout)
    public:
        explicit ImportPlanView(pipeline::ImportPlan* plan) : m_plan(plan)
        {
            Direction = ui::Orientation::Vertical;
            Spacing = 2.0f;
            m_nameEditors.Resize(plan->entries.Size());
            constexpr pipeline::ImportResourceKind kKinds[] = {
                pipeline::ImportResourceKind::Mesh,     pipeline::ImportResourceKind::Material,
                pipeline::ImportResourceKind::Texture,  pipeline::ImportResourceKind::Skeleton,
                pipeline::ImportResourceKind::AnimationClip,
                pipeline::ImportResourceKind::Collision,
            };
            for (const pipeline::ImportResourceKind kind : kKinds)
            {
                Array<usize> indices;
                for (usize i = 0; i < plan->entries.Size(); ++i)
                {
                    if (plan->entries[i].kind == kind)
                    {
                        indices.PushBack(i);
                    }
                }
                if (indices.IsEmpty())
                {
                    continue;
                }

                Array<RefPtr<ui::CheckBox>> rowChecks;
                rowChecks.Reserve(indices.Size());
                auto header = MakeRef<ui::CheckBox>(
                    MemoryAllocator(), pipeline::ImportResourceKindLabel(kind), true);
                header->FontSize.SetValue(12.0f);
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Match();
                    lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(24.0f));
                    AddView(header.Get(), lp);
                }
                for (const usize i : indices)
                {
                    pipeline::ImportPlanEntry* entry = &plan->entries[i];
                    auto row = MakeRef<ui::FlexLayout>(MemoryAllocator());
                    row->Direction = ui::Orientation::Horizontal;
                    row->Spacing = 6.0f;
                    auto check = MakeRef<ui::CheckBox>(MemoryAllocator(), u8"", entry->enabled);
                    check->OnCheckedChanged.Add([entry](ui::CheckBox*, bool checked)
                                                { entry->enabled = checked; });
                    {
                        ui::LayoutStyle lp;
                        lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(38.0f));
                        lp.Height = ui::SizeSpec::Match();
                        row->AddView(check.Get(), lp);
                    }
                    rowChecks.PushBack(check);
                    auto name = MakeRef<ui::EditText>(MemoryAllocator());
                    name->SetText(entry->targetName.AsView());
                    {
                        ui::LayoutStyle lp;
                        lp.FlexGrow = 1.0f;
                        lp.Height = ui::SizeSpec::Match();
                        row->AddView(name.Get(), lp);
                    }
                    m_nameEditors[i] = name;
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Match();
                    lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(24.0f));
                    AddView(row.Get(), lp);
                }
                pipeline::ImportPlan* planRef = m_plan;
                header->OnCheckedChanged.Add(
                    [planRef, indices = Move(indices), rowChecks = Move(rowChecks)](ui::CheckBox*,
                                                                                    bool checked)
                    {
                        for (const usize i : indices)
                        {
                            planRef->entries[i].enabled = checked;
                        }
                        for (const RefPtr<ui::CheckBox>& c : rowChecks)
                        {
                            c->IsChecked.SetValue(checked);
                        }
                    });
            }
        }

        /// Write the name editors' current text back into the plan entries.
        void SyncNames()
        {
            for (usize i = 0; i < m_nameEditors.Size() && i < m_plan->entries.Size(); ++i)
            {
                if (m_nameEditors[i].Get() != nullptr)
                {
                    const StringView text = m_nameEditors[i]->Text();
                    if (!text.IsEmpty())
                    {
                        m_plan->entries[i].targetName = String(text);
                    }
                }
            }
        }

    private:
        pipeline::ImportPlan* m_plan; // borrowed; outlives the view
        Array<RefPtr<ui::EditText>> m_nameEditors; // parallel to plan entries
    };

    class ImportOptionsDialog final : public ui::Dialog
    {
        RTTI_OBJECT(ImportOptionsDialog, ui::Dialog)
    public:
        /// Fired when the user confirms; the options object carries their checkbox edits.
        Function<void()> OnImport;
        /// Fired when the user clicks "Change..." on the destination row. The caller (which knows the
        /// project's groups) pops a group chooser, then calls SetDestination with the new group path.
        Function<void()> OnChangeDestination;

        /// Update the shown destination after the caller's group chooser resolves.
        void SetDestination(StringView destination)
        {
            if (m_destinationText.Get() != nullptr)
            {
                m_destinationText->SetText(destination);
            }
        }

        ImportOptionsDialog(StringView sourcePath, StringView destination,
                            RefPtr<pipeline::ImportOptions> options,
                            pipeline::ImportPlan plan = {})
            : ui::Dialog(u8"Import"), m_options(Move(options)), m_plan(Move(plan))
        {
            const bool hasPlan = !m_plan.IsEmpty();
            MaxWidth.SetValue(hasPlan ? 620.0f : 480.0f);
            MaxHeight.SetValue(hasPlan ? 700.0f : 420.0f);

            auto column = MakeRef<ui::FlexLayout>(MemoryAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6.0f;

            AddInfoRow(*column, u8"Source:", pipeline::FileNameOf(sourcePath));

            // Destination row: "Into: <group path> [Change...]" - lets the user retarget the import
            // (default = the active group). The caller wires OnChangeDestination to a group chooser.
            {
                auto row = MakeRef<ui::FlexLayout>(MemoryAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6.0f;
                auto name = MakeRef<ui::Label>(MemoryAllocator(), StringView(u8"Into:"));
                name->FontSize.SetValue(11.0f);
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(52.0f));
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(name.Get(), lp);
                }
                m_destinationText = MakeRef<ui::Label>(MemoryAllocator(), destination);
                m_destinationText->FontSize.SetValue(11.0f);
                m_destinationText->Ellipsis.SetValue(true);
                {
                    ui::LayoutStyle lp;
                    lp.FlexGrow = 1.0f;
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(m_destinationText.Get(), lp);
                }
                auto change = MakeRef<ui::Button>(MemoryAllocator(), StringView(u8"Change..."));
                change->FontSize.SetValue(Optional<f32>{11.0f});
                ImportOptionsDialog* self = this;
                change->OnClick.Add([self](ui::ButtonBase*)
                                    { if (self->OnChangeDestination) self->OnChangeDestination(); });
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(72.0f));
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(change.Get(), lp);
                }
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
                column->AddView(row.Get(), lp);
            }

            if (hasPlan)
            {
                BuildResourceList(*column);
            }

            if (m_options.Get() != nullptr)
            {
                for (const pipeline::ImportOptions::Toggle& toggle : m_options->Toggles())
                {
                    if (toggle.value == nullptr)
                    {
                        continue;
                    }
                    auto check =
                        MakeRef<ui::CheckBox>(MemoryAllocator(), toggle.label, *toggle.value);
                    check->FontSize.SetValue(12.0f);
                    if (!toggle.description.IsEmpty())
                    {
                        check->TooltipText = String(toggle.description);
                    }
                    bool* value = toggle.value;
                    check->OnCheckedChanged.Add([value](ui::CheckBox*, bool checked)
                                                { *value = checked; });
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Match();
                    lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
                    column->AddView(check.Get(), lp);
                }
            }

            SetContent(column.Get());

            ImportOptionsDialog* self = this;
            ui::Button* import = AddButton(u8"Import", ui::DialogResult::None);
            import->OnClick.Add(
                [self](ui::ButtonBase*)
                {
                    if (self->OnImport)
                    {
                        self->OnImport();
                    }
                    self->Close(ui::DialogResult::OK);
                });
            AddButton(u8"Cancel", ui::DialogResult::Cancel);
        }

        [[nodiscard]] pipeline::ImportOptions* Options() const noexcept
        {
            return m_options.Get();
        }

        /// The edited plan (checkboxes applied live; names synced from their editors here).
        /// Call once, on Import.
        [[nodiscard]] pipeline::ImportPlan TakePlan()
        {
            if (m_planView.Get() != nullptr)
            {
                m_planView->SyncNames();
            }
            return static_cast<pipeline::ImportPlan&&>(m_plan);
        }

    private:
        void BuildResourceList(ui::FlexLayout& column)
        {
            m_planView = MakeRef<ImportPlanView>(MemoryAllocator(), &m_plan);
            auto scroll = MakeRef<ui::ScrollView>(MemoryAllocator());
            scroll->VScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
            scroll->HScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Never);
            {
                ui::LayoutStyle contentLp;
                contentLp.Width = ui::SizeSpec::Match();
                scroll->AddView(m_planView.Get(), contentLp);
            }
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            lp.FlexGrow = 1.0f;
            column.AddView(scroll.Get(), lp);
        }

        void AddInfoRow(ui::FlexLayout& column, StringView label, StringView value)
        {
            auto row = MakeRef<ui::FlexLayout>(MemoryAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 6.0f;

            auto name = MakeRef<ui::Label>(MemoryAllocator(), label);
            name->FontSize.SetValue(11.0f);
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(52.0f));
                lp.Height = ui::SizeSpec::Match();
                row->AddView(name.Get(), lp);
            }
            auto text = MakeRef<ui::Label>(MemoryAllocator(), value);
            text->FontSize.SetValue(11.0f);
            text->Ellipsis.SetValue(true);
            {
                ui::LayoutStyle lp;
                lp.FlexGrow = 1.0f;
                lp.Height = ui::SizeSpec::Match();
                row->AddView(text.Get(), lp);
            }
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(18.0f));
            column.AddView(row.Get(), lp);
        }

        RefPtr<pipeline::ImportOptions> m_options;
        RefPtr<ui::Label> m_destinationText; // the shown group path; updated by SetDestination
        pipeline::ImportPlan m_plan;         // edited in place; handed back via TakePlan
        RefPtr<ImportPlanView> m_planView;
    };

    /// A selectable source-file row in the batch list: SELECTION is drawn (accent fill), not
    /// implied - a Button per file could not show which file's detail was active. Hosts the
    /// enable checkbox / name label / importer dropdown as children; clicks the children do
    /// not consume (the label, blank space) select the row via the bubble phase.
    class BatchFileRow final : public ui::FlexLayout
    {
        RTTI_OBJECT(BatchFileRow, ui::FlexLayout)
    public:
        Function<void()> OnSelect;

        BatchFileRow()
        {
            Direction = ui::Orientation::Horizontal;
            Spacing = 4.0f;
        }

        void SetSelected(bool selected)
        {
            if (m_selected != selected)
            {
                m_selected = selected;
                InvalidateVisual(); // highlight only - geometry unchanged
            }
        }
        [[nodiscard]] bool IsSelected() const noexcept { return m_selected; }

        void OnDraw(ui::UIDrawContext& ctx) override
        {
            if (m_selected)
            {
                const Color accent = ResolveStyleColor(
                    ui::StyleProperty::AccentColor,
                    Color{60.0f / 255.0f, 120.0f / 255.0f, 200.0f / 255.0f, 100.0f / 255.0f});
                ctx.VG().FillRoundedRect(Rectangle{0, 0, Width(), Height()}, 3.0f,
                                         Color{accent.r, accent.g, accent.b, 0.35f});
            }
            ui::FlexLayout::OnDraw(ctx);
        }

        void OnMouseDown(ui::MouseEventArgs& e) override
        {
            if (e.Button == ui::MouseButton::Left)
            {
                if (OnSelect)
                {
                    OnSelect();
                }
                e.Handled = true;
            }
        }

    private:
        bool m_selected = false;
    };

    /// One review session for a whole DROP (the import-workflow ruling: one dialog per drop,
    /// always). Left: the source files - enable checkbox, name, and an importer dropdown
    /// where more than one importer claims the extension (this ABSORBS the old
    /// modal-per-file chooser). Right: the selected file's resource plan + option toggles.
    /// Worker-prepared plans stream in (OnFilePrepared); Import stays disabled until every
    /// enabled file is described. The caller reads Files() back on OnImport and commits each
    /// enabled entry.
    class BatchImportDialog final : public ui::Dialog
    {
        RTTI_OBJECT(BatchImportDialog, ui::Dialog)
    public:
        struct FileEntry
        {
            String path;
            Array<pipeline::IFileImporter*> candidates; // >= 1, registry order
            usize importerIndex = 0;
            bool enabled = true;
            RefPtr<pipeline::ImportOptions> options; // for the CURRENT importer choice
            RefPtr<Object> prepared;                 // worker payload (heavy importers)
            pipeline::ImportPlan plan;
            bool described = false; // plan/toggles are ready to show + commit
        };

        Function<void()> OnImport; // read Files() for the final decisions
        Function<void()> OnChangeDestination;
        /// (Re)describe one entry - the OWNER owns describe policy (inline for light
        /// importers; the worker path lands through OnFilePrepared instead).
        Function<void(FileEntry&)> DescribeFile;

        BatchImportDialog(StringView destination, Array<FileEntry> files)
            : ui::Dialog(u8"Import Files"), m_files(Move(files))
        {
            MaxWidth.SetValue(860.0f);
            MaxHeight.SetValue(700.0f);

            auto column = MakeRef<ui::FlexLayout>(MemoryAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6.0f;

            // Destination row (shared by the whole batch).
            {
                auto row = MakeRef<ui::FlexLayout>(MemoryAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6.0f;
                auto name = MakeRef<ui::Label>(MemoryAllocator(), StringView(u8"Into:"));
                name->FontSize.SetValue(11.0f);
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(52.0f));
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(name.Get(), lp);
                }
                m_destinationText = MakeRef<ui::Label>(MemoryAllocator(), destination);
                m_destinationText->FontSize.SetValue(11.0f);
                m_destinationText->Ellipsis.SetValue(true);
                {
                    ui::LayoutStyle lp;
                    lp.FlexGrow = 1.0f;
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(m_destinationText.Get(), lp);
                }
                auto change = MakeRef<ui::Button>(MemoryAllocator(), StringView(u8"Change..."));
                change->FontSize.SetValue(Optional<f32>{11.0f});
                BatchImportDialog* self = this;
                change->OnClick.Add([self](ui::ButtonBase*)
                                    { if (self->OnChangeDestination) self->OnChangeDestination(); });
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(72.0f));
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(change.Get(), lp);
                }
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
                column->AddView(row.Get(), lp);
            }

            // The split: file list left, per-file detail right.
            auto split = MakeRef<ui::FlexLayout>(MemoryAllocator());
            split->Direction = ui::Orientation::Horizontal;
            split->Spacing = 8.0f;
            BuildFileList(*split);
            m_detail = MakeRef<ui::FlexLayout>(MemoryAllocator());
            m_detail->Direction = ui::Orientation::Vertical;
            m_detail->Spacing = 4.0f;
            {
                auto scroll = MakeRef<ui::ScrollView>(MemoryAllocator());
                scroll->VScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
                scroll->HScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Never);
                {
                    ui::LayoutStyle contentLp;
                    contentLp.Width = ui::SizeSpec::Match();
                    scroll->AddView(m_detail.Get(), contentLp);
                }
                ui::LayoutStyle lp;
                lp.FlexGrow = 1.0f;
                lp.Height = ui::SizeSpec::Match();
                split->AddView(scroll.Get(), lp);
            }
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.FlexGrow = 1.0f;
                column->AddView(split.Get(), lp);
            }
            SetContent(column.Get());

            BatchImportDialog* self = this;
            m_importButton = AddButton(u8"Import", ui::DialogResult::None);
            m_importButton->OnClick.Add(
                [self](ui::ButtonBase*)
                {
                    self->SyncSelectedNames();
                    if (self->OnImport)
                    {
                        self->OnImport();
                    }
                    self->Close(ui::DialogResult::OK);
                });
            AddButton(u8"Cancel", ui::DialogResult::Cancel);

            RebuildDetail();
            SyncImportEnabled();
        }

        [[nodiscard]] Array<FileEntry>& Files() noexcept { return m_files; }

        void SetDestination(StringView destination)
        {
            if (m_destinationText.Get() != nullptr)
            {
                m_destinationText->SetText(destination);
            }
        }

        /// A worker prepare landed for `index`: the owner filled prepared/plan/described.
        /// Refreshes the detail (if that file is selected) and the Import gate.
        void OnFilePrepared(usize index)
        {
            if (index == m_selected)
            {
                QueueRebuildDetail();
            }
            SyncImportEnabled();
        }

        /// Describe every file through DescribeFile (the owner assigns it AFTER construction, so
        /// the constructor's detail + Import gate saw nothing described), then refresh both. The
        /// owner calls this once before Show; worker-prepared files stay "Reading..." until their
        /// OnFilePrepared. Without this refresh an inline importer (textures) left the dialog
        /// gated forever: Import disabled, detail stuck on "Reading file..." (2026-09-21).
        void DescribeAll()
        {
            if (DescribeFile)
            {
                for (FileEntry& entry : m_files)
                {
                    DescribeFile(entry);
                }
            }
            QueueRebuildDetail();
            SyncImportEnabled();
        }

        /// The Import gate as the button shows it (every enabled file described).
        [[nodiscard]] bool ImportEnabled() const noexcept
        {
            return m_importButton != nullptr && m_importButton->IsEnabled;
        }

    private:
        void BuildFileList(ui::FlexLayout& split)
        {
            auto list = MakeRef<ui::FlexLayout>(MemoryAllocator());
            list->Direction = ui::Orientation::Vertical;
            list->Spacing = 2.0f;
            BatchImportDialog* self = this;
            for (usize i = 0; i < m_files.Size(); ++i)
            {
                FileEntry* entry = &m_files[i];
                auto row = MakeRef<BatchFileRow>(MemoryAllocator());
                row->OnSelect = [self, i]() { self->SelectFile(i); };
                row->SetSelected(i == m_selected);
                m_fileRows.PushBack(row);
                auto check = MakeRef<ui::CheckBox>(MemoryAllocator(), u8"", entry->enabled);
                check->OnCheckedChanged.Add(
                    [self, entry](ui::CheckBox*, bool checked)
                    {
                        entry->enabled = checked;
                        self->SyncImportEnabled();
                    });
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(30.0f));
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(check.Get(), lp);
                }
                auto name = MakeRef<ui::Label>(MemoryAllocator(),
                                               pipeline::FileNameOf(entry->path.AsView()));
                name->FontSize.SetValue(12.0f);
                name->Ellipsis.SetValue(true);
                {
                    ui::LayoutStyle lp;
                    lp.FlexGrow = 1.0f;
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(name.Get(), lp);
                }
                if (entry->candidates.Size() > 1)
                {
                    // Multiple importers claim the extension: the per-row dropdown replaces
                    // the old modal-per-file chooser.
                    auto combo = MakeRef<ui::ComboBox>(MemoryAllocator());
                    for (pipeline::IFileImporter* importer : entry->candidates)
                    {
                        (void)combo->AddItem(importer->Label());
                    }
                    combo->SetSelectedIndex(static_cast<i32>(entry->importerIndex));
                    combo->OnSelectionChanged.Add(
                        [self, i](ui::ComboBox*, i32 index)
                        { self->ChangeImporter(i, static_cast<usize>(Max(0, index))); });
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(110.0f));
                    lp.Height = ui::SizeSpec::Match();
                    row->AddView(combo.Get(), lp);
                }
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(26.0f));
                list->AddView(row.Get(), lp);
            }
            auto scroll = MakeRef<ui::ScrollView>(MemoryAllocator());
            scroll->VScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
            scroll->HScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Never);
            {
                ui::LayoutStyle contentLp;
                contentLp.Width = ui::SizeSpec::Match();
                scroll->AddView(list.Get(), contentLp);
            }
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(300.0f));
            lp.Height = ui::SizeSpec::Match();
            split.AddView(scroll.Get(), lp);
        }

        void SelectFile(usize index)
        {
            if (index == m_selected || index >= m_files.Size())
            {
                return;
            }
            SyncSelectedNames(); // keep this file's rename edits before switching away
            if (m_selected < m_fileRows.Size())
            {
                m_fileRows[m_selected]->SetSelected(false);
            }
            m_selected = index;
            if (m_selected < m_fileRows.Size())
            {
                m_fileRows[m_selected]->SetSelected(true);
            }
            QueueRebuildDetail();
        }

        void ChangeImporter(usize fileIndex, usize importerIndex)
        {
            FileEntry& entry = m_files[fileIndex];
            if (importerIndex >= entry.candidates.Size() ||
                importerIndex == entry.importerIndex)
            {
                return;
            }
            entry.importerIndex = importerIndex;
            entry.options = entry.candidates[importerIndex]->CreateOptions(MemoryAllocator());
            if (entry.options.Get() == nullptr)
            {
                // Bare selection carrier (option-less importers still honor renames).
                entry.options = RefPtr<pipeline::ImportOptions>(
                    MakeRef<pipeline::ImportOptions>(MemoryAllocator()).Get());
            }
            entry.prepared = {}; // payloads are importer-specific
            entry.plan = {};
            entry.described = false;
            if (DescribeFile)
            {
                DescribeFile(entry);
            }
            if (fileIndex == m_selected)
            {
                QueueRebuildDetail();
            }
            SyncImportEnabled();
        }

        // Detail rebuilds DESTROY views - never inline from a click handler (the
        // mutation-queue rule).
        void QueueRebuildDetail()
        {
            if (Context == nullptr)
            {
                RebuildDetail();
                return;
            }
            BatchImportDialog* self = this;
            const RefPtr<BatchImportDialog> keepAlive(this);
            Context->MutationQueueRef().QueueAction(
                Function<void()>{[self, keepAlive]() { self->RebuildDetail(); }});
        }

        void RebuildDetail()
        {
            m_detail->RemoveAllViews();
            m_planView = {};
            if (m_selected >= m_files.Size())
            {
                return;
            }
            FileEntry& entry = m_files[m_selected];

            auto title = MakeRef<ui::Label>(MemoryAllocator(),
                                            pipeline::FileNameOf(entry.path.AsView()));
            title->FontSize.SetValue(13.0f);
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
                m_detail->AddView(title.Get(), lp);
            }

            if (!entry.described)
            {
                auto reading =
                    MakeRef<ui::Label>(MemoryAllocator(), StringView(u8"Reading file..."));
                reading->FontSize.SetValue(12.0f);
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
                m_detail->AddView(reading.Get(), lp);
                return;
            }

            if (!entry.plan.IsEmpty())
            {
                m_planView = MakeRef<ImportPlanView>(MemoryAllocator(), &entry.plan);
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                m_detail->AddView(m_planView.Get(), lp);
            }
            if (entry.options.Get() != nullptr)
            {
                for (const pipeline::ImportOptions::Toggle& toggle : entry.options->Toggles())
                {
                    if (toggle.value == nullptr)
                    {
                        continue;
                    }
                    auto check =
                        MakeRef<ui::CheckBox>(MemoryAllocator(), toggle.label, *toggle.value);
                    check->FontSize.SetValue(12.0f);
                    if (!toggle.description.IsEmpty())
                    {
                        check->TooltipText = String(toggle.description);
                    }
                    bool* value = toggle.value;
                    check->OnCheckedChanged.Add([value](ui::CheckBox*, bool checked)
                                                { *value = checked; });
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Match();
                    lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
                    m_detail->AddView(check.Get(), lp);
                }
            }
        }

        void SyncSelectedNames()
        {
            if (m_planView.Get() != nullptr)
            {
                m_planView->SyncNames();
            }
        }

        // Import waits for every ENABLED file to be described (worker prepares stream in).
        void SyncImportEnabled()
        {
            if (m_importButton == nullptr)
            {
                return;
            }
            bool ready = true;
            for (const FileEntry& f : m_files)
            {
                ready = ready && (!f.enabled || f.described);
            }
            m_importButton->IsEnabled = ready;
            m_importButton->Invalidate();
        }

        Array<FileEntry> m_files;
        usize m_selected = 0;
        RefPtr<ui::Label> m_destinationText;
        RefPtr<ui::FlexLayout> m_detail;
        RefPtr<ImportPlanView> m_planView;       // the SELECTED file's plan view
        Array<RefPtr<BatchFileRow>> m_fileRows;  // parallel to m_files (selection highlight)
        ui::Button* m_importButton = nullptr;
    };

    RTTI_DEFINE_OBJECT(ImportPlanView, "rtti::editor::editor::app")
    RTTI_DEFINE_OBJECT(BatchFileRow, "rtti::editor::editor::app")
    RTTI_DEFINE_OBJECT(ImportOptionsDialog, "rtti::editor::editor::app")
    RTTI_DEFINE_OBJECT(BatchImportDialog, "rtti::editor::editor::app")
}
