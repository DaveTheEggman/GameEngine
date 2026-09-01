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

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6.0f;

            AddInfoRow(*column, u8"Source:", pipeline::FileNameOf(sourcePath));

            // Destination row: "Into: <group path> [Change...]" - lets the user retarget the import
            // (default = the active group). The caller wires OnChangeDestination to a group chooser.
            {
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6.0f;
                auto name = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Into:"));
                name->FontSize.SetValue(11.0f);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(52.0f));
                    lp->Height = ui::SizeSpec::Match();
                    row->AddView(name.Get(), lp);
                }
                m_destinationText = MakeRef<ui::Label>(DefaultAllocator(), destination);
                m_destinationText->FontSize.SetValue(11.0f);
                m_destinationText->Ellipsis.SetValue(true);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Grow = 1.0f;
                    lp->Height = ui::SizeSpec::Match();
                    row->AddView(m_destinationText.Get(), lp);
                }
                auto change = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Change..."));
                change->FontSize.SetValue(Optional<f32>{11.0f});
                ImportOptionsDialog* self = this;
                change->OnClick.Add([self](ui::ButtonBase*)
                                    { if (self->OnChangeDestination) self->OnChangeDestination(); });
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(72.0f));
                    lp->Height = ui::SizeSpec::Match();
                    row->AddView(change.Get(), lp);
                }
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
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
                        MakeRef<ui::CheckBox>(DefaultAllocator(), toggle.label, *toggle.value);
                    check->FontSize.SetValue(12.0f);
                    if (!toggle.description.IsEmpty())
                    {
                        check->TooltipText = String(toggle.description);
                    }
                    bool* value = toggle.value;
                    check->OnCheckedChanged.Add([value](ui::CheckBox*, bool checked)
                                                { *value = checked; });
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
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

        /// The edited plan (checkboxes applied live; names read from their editors here).
        /// Call once, on Import.
        [[nodiscard]] pipeline::ImportPlan TakePlan()
        {
            for (usize i = 0; i < m_nameEditors.Size() && i < m_plan.entries.Size(); ++i)
            {
                if (m_nameEditors[i].Get() != nullptr)
                {
                    const StringView text = m_nameEditors[i]->Text();
                    if (!text.IsEmpty())
                    {
                        m_plan.entries[i].targetName = String(text);
                    }
                }
            }
            return static_cast<pipeline::ImportPlan&&>(m_plan);
        }

    private:
        // The per-kind resource sections: a check-all header per kind, then one row per
        // resource (enable checkbox + target-name editor). Scrolls - a character model
        // brings dozens of clips.
        void BuildResourceList(ui::FlexLayout& column)
        {
            auto list = MakeRef<ui::FlexLayout>(DefaultAllocator());
            list->Direction = ui::Orientation::Vertical;
            list->Spacing = 2.0f;

            m_nameEditors.Resize(m_plan.entries.Size());
            constexpr pipeline::ImportResourceKind kKinds[] = {
                pipeline::ImportResourceKind::Mesh,          pipeline::ImportResourceKind::Material,
                pipeline::ImportResourceKind::Texture,       pipeline::ImportResourceKind::Skeleton,
                pipeline::ImportResourceKind::AnimationClip, pipeline::ImportResourceKind::Collision,
            };
            for (const pipeline::ImportResourceKind kind : kKinds)
            {
                // Collect this kind's entry indices (the plan arrives in fan-out order).
                Array<usize> indices;
                for (usize i = 0; i < m_plan.entries.Size(); ++i)
                {
                    if (m_plan.entries[i].kind == kind)
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
                    DefaultAllocator(), pipeline::ImportResourceKindLabel(kind), true);
                header->FontSize.SetValue(12.0f);
                {
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(24.0f));
                    list->AddView(header.Get(), lp);
                }

                for (const usize i : indices)
                {
                    pipeline::ImportPlanEntry* entry = &m_plan.entries[i];
                    auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                    row->Direction = ui::Orientation::Horizontal;
                    row->Spacing = 6.0f;
                    auto check =
                        MakeRef<ui::CheckBox>(DefaultAllocator(), u8"", entry->enabled);
                    check->OnCheckedChanged.Add([entry](ui::CheckBox*, bool checked)
                                                { entry->enabled = checked; });
                    {
                        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                        lp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(38.0f));
                        lp->Height = ui::SizeSpec::Match();
                        row->AddView(check.Get(), lp);
                    }
                    rowChecks.PushBack(check);
                    auto name = MakeRef<ui::EditText>(DefaultAllocator());
                    name->SetText(entry->targetName.AsView());
                    {
                        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                        lp->Grow = 1.0f;
                        lp->Height = ui::SizeSpec::Match();
                        row->AddView(name.Get(), lp);
                    }
                    m_nameEditors[i] = name;
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Width = ui::SizeSpec::Match();
                    lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(24.0f));
                    list->AddView(row.Get(), lp);
                }

                // Check-all: drives every row of the section (rows update visually too).
                pipeline::ImportPlan* plan = &m_plan;
                header->OnCheckedChanged.Add(
                    [plan, indices = Move(indices), rowChecks = Move(rowChecks)](ui::CheckBox*,
                                                                                bool checked)
                    {
                        for (const usize i : indices)
                        {
                            plan->entries[i].enabled = checked;
                        }
                        for (const RefPtr<ui::CheckBox>& c : rowChecks)
                        {
                            c->IsChecked.SetValue(checked);
                        }
                    });
            }

            auto scroll = MakeRef<ui::ScrollView>(DefaultAllocator());
            scroll->VScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Auto);
            scroll->HScrollBarPolicy.SetValue(ui::ScrollBarPolicy::Never);
            {
                auto contentLp = MakeRef<ui::LayoutParams>(DefaultAllocator());
                contentLp->Width = ui::SizeSpec::Match();
                scroll->AddView(list.Get(), contentLp);
            }
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Grow = 1.0f;
            column.AddView(scroll.Get(), lp);
        }

        void AddInfoRow(ui::FlexLayout& column, StringView label, StringView value)
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 6.0f;

            auto name = MakeRef<ui::Label>(DefaultAllocator(), label);
            name->FontSize.SetValue(11.0f);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(52.0f));
                lp->Height = ui::SizeSpec::Match();
                row->AddView(name.Get(), lp);
            }
            auto text = MakeRef<ui::Label>(DefaultAllocator(), value);
            text->FontSize.SetValue(11.0f);
            text->Ellipsis.SetValue(true);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                lp->Height = ui::SizeSpec::Match();
                row->AddView(text.Get(), lp);
            }
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(18.0f));
            column.AddView(row.Get(), lp);
        }

        RefPtr<pipeline::ImportOptions> m_options;
        RefPtr<ui::Label> m_destinationText; // the shown group path; updated by SetDestination
        pipeline::ImportPlan m_plan;         // edited in place; handed back via TakePlan
        Array<RefPtr<ui::EditText>> m_nameEditors; // parallel to m_plan.entries (null = no row)
    };

    RTTI_DEFINE_OBJECT(ImportOptionsDialog, "rtti::editor::editor::app")
}
