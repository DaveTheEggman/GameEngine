// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :log_view partition.
//
// LogView: the Console panel content - a log view on foundation.ui, with category display
// (core logs carry categories). A
// filter/action toolbar (per-level CheckBoxes + Clear) over a recycled ListView of level-colored
// rows; bounded entry count; auto-scroll to the newest entry. Fed once per frame by
// EditorApplication draining the EditorLogBuffer. Rows select like any list (click, Ctrl click,
// Shift click, Ctrl+A), and Ctrl+C or the context menu's Copy puts the selected rows on the
// clipboard, oldest first, one per line.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:log_view;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;

    class LogView : public ui::ViewGroup
    {
        RTTI_OBJECT(LogView, ui::ViewGroup)
    public:
        /// Display buckets (core Trace+Debug fold into Debug; Error+Fatal into Error).
        enum class Bucket : u8
        {
            Debug,
            Info,
            Warning,
            Error
        };
        static constexpr usize kBucketCount = 4;

        [[nodiscard]] static Bucket BucketOf(LogLevel level) noexcept
        {
            switch (level)
            {
            case LogLevel::Trace:
            case LogLevel::Debug:
                return Bucket::Debug;
            case LogLevel::Info:
                return Bucket::Info;
            case LogLevel::Warning:
                return Bucket::Warning;
            default:
                return Bucket::Error;
            }
        }

        LogView()
        {
            auto column = MakeRef<ui::FlexLayout>(MemoryAllocator());
            column->Direction = ui::Orientation::Vertical;

            // Toolbar: level filters + Clear.
            auto toolbar = MakeRef<ui::FlexLayout>(MemoryAllocator());
            toolbar->Direction = ui::Orientation::Horizontal;
            toolbar->Spacing = 8.0f;
            toolbar->Padding = ui::Thickness{4, 4};
            static constexpr const char8_t* kNames[kBucketCount] = {u8"Debug", u8"Info",
                                                                    u8"Warning", u8"Error"};
            for (usize i = 0; i < kBucketCount; ++i)
            {
                auto box = MakeRef<ui::CheckBox>(MemoryAllocator(), StringView(kNames[i]), true);
                const usize bucket = i;
                box->OnCheckedChanged.Add(
                    [this, bucket](ui::CheckBox*, bool checked)
                    { SetBucketVisible(static_cast<Bucket>(bucket), checked); });
                m_filterBoxes[i] = box.Get();
                toolbar->AddView(box.Get());
            }
            auto clear = MakeRef<ui::Button>(MemoryAllocator(), StringView(u8"Clear"));
            clear->OnClick.Add([this](ui::ButtonBase*) { Clear(); });
            toolbar->AddView(clear.Get());
            column->AddView(toolbar.Get());

            // The entry list (recycled rows), selecting many.
            m_adapter = MakeUnique<Adapter>(MemoryAllocator(), *this);
            m_list = MakeRef<EntryList>(MemoryAllocator(), *this);
            m_list->ItemHeight.SetValue(20.0f);
            m_list->Selection.Mode = ui::SelectionMode::Multiple;
            m_list->SetAdapter(m_adapter.Get());
            m_list->OnItemRightClicked.Add([this](i32, f32 x, f32 y) { ShowContextMenu(x, y); });
            m_list->OnBackgroundRightClicked.Add([this](f32 x, f32 y) { ShowContextMenu(x, y); });
            {
                ui::LayoutStyle grow;
                grow.FlexGrow = 1.0f;
                column->AddView(m_list.Get(), grow);
            }

            AddView(column.Get());
        }

        ~LogView() override { m_list->SetAdapter(nullptr); }

        /// Says a copy happened (the app's toast): the number of lines copied.
        Function<void(usize)> OnCopied;

        /// True while the newest entry is kept in view.
        bool AutoScroll = true;

        /// Cap on retained entries (oldest trimmed).
        usize MaxEntries = 1000;

        void AddEntry(LogLevel level, StringView category, StringView message)
        {
            Entry entry;
            entry.bucket = BucketOf(level);
            entry.text = String(u8"[");
            entry.text += category;
            entry.text += u8"] ";
            entry.text += message;
            m_entries.PushBack(Move(entry));

            // Trim to cap: indices into m_entries shift, so the filter is rebuilt below, and the
            // selection moves up by the visible rows that went (a trimmed selected row is gone).
            bool trimmed = false;
            i32 trimmedVisible = 0;
            while (m_entries.Size() > MaxEntries)
            {
                if (IsBucketVisible(m_entries[0].bucket))
                {
                    ++trimmedVisible;
                }
                m_entries.RemoveAt(0);
                trimmed = true;
            }

            if (trimmed)
            {
                m_list->Selection.ShiftIndices(0, -trimmedVisible);
                RebuildFilter();
            }
            else if (IsBucketVisible(m_entries.Back().bucket))
            {
                m_filtered.PushBack(m_entries.Size() - 1);
                m_adapter->NotifyDataSetChanged();
            }
            ScrollToNewest();
        }

        void Clear()
        {
            m_entries.Clear();
            m_filtered.Clear();
            m_list->Selection.ClearSelection();
            m_adapter->NotifyDataSetChanged();
        }

        /// Selects every visible row.
        void SelectAll()
        {
            if (!m_filtered.IsEmpty())
            {
                m_list->Selection.SelectRange(0, static_cast<i32>(m_filtered.Size()) - 1);
            }
        }

        [[nodiscard]] usize SelectedCount() const noexcept
        {
            return m_list->Selection.SelectedCount();
        }
        /// The entry list: its selection, and the keys it takes (the tests drive both).
        [[nodiscard]] ui::ListView& List() noexcept { return *m_list; }

        /// The selected rows' text, oldest first, one per line.
        [[nodiscard]] String SelectedText() const
        {
            Array<i32> positions;
            for (i32 position : m_list->Selection.SelectedPositions())
            {
                if (position >= 0 && static_cast<usize>(position) < m_filtered.Size())
                {
                    positions.PushBack(position);
                }
            }
            positions.Sort([](i32 a, i32 b) { return a < b; });
            String text;
            for (i32 position : positions)
            {
                if (!text.IsEmpty())
                {
                    text.Append(u8"\n");
                }
                text.Append(m_entries[m_filtered[static_cast<usize>(position)]].text.AsView());
            }
            return text;
        }

        /// Puts the selected rows on the system clipboard; nothing selected copies nothing.
        void CopySelection()
        {
            const String text = SelectedText();
            ui::IClipboard* clipboard = Context != nullptr ? Context->Clipboard() : nullptr;
            if (text.IsEmpty() || clipboard == nullptr)
            {
                return;
            }
            if (clipboard->SetText(text.AsView()).IsOk() && OnCopied)
            {
                OnCopied(SelectedCount());
            }
        }

        void SetBucketVisible(Bucket bucket, bool visible)
        {
            m_visible[static_cast<usize>(bucket)] = visible;
            // A filter change re-numbers the rows, so the selection goes.
            m_list->Selection.ClearSelection();
            if (ui::CheckBox* box = m_filterBoxes[static_cast<usize>(bucket)])
            {
                box->IsChecked.SetSilent(visible); // keep the toolbar in sync on programmatic calls
            }
            RebuildFilter();
            ScrollToNewest();
        }

        [[nodiscard]] bool IsBucketVisible(Bucket bucket) const noexcept
        {
            return m_visible[static_cast<usize>(bucket)];
        }

        [[nodiscard]] usize EntryCount() const noexcept { return m_entries.Size(); }
        [[nodiscard]] usize VisibleEntryCount() const noexcept { return m_filtered.Size(); }

        [[nodiscard]] StringView VisibleEntryText(usize visibleIndex) const
        {
            return m_entries[m_filtered[visibleIndex]].text.AsView();
        }

        // Fill the available space (the default ViewGroup measure wraps to children, which would
        // collapse the virtualized list); the single child column fills us.
        void OnMeasure(ui::BoxConstraints constraints) override
        {
            for (usize i = 0; i < ChildCount(); ++i)
            {
                GetChildAt(i)->Measure(constraints);
            }
            MeasuredSize = Float2{constraints.MaxWidth, constraints.MaxHeight};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            for (usize i = 0; i < ChildCount(); ++i)
            {
                GetChildAt(i)->Layout(0, 0, width, height);
            }
        }

    private:
        // The entry list: Ctrl+C copies the selection and Ctrl+A selects every row.
        class EntryList final : public ui::ListView
        {
        public:
            explicit EntryList(LogView& owner) : m_owner(&owner) {}

            void OnKeyDown(ui::KeyEventArgs& e) override
            {
                if (ui::HasFlag(e.Modifiers, ui::KeyModifiers::Ctrl) &&
                    !ui::HasFlag(e.Modifiers, ui::KeyModifiers::Alt))
                {
                    if (e.Key == ui::KeyCode::C)
                    {
                        m_owner->CopySelection();
                        e.Handled = true;
                        return;
                    }
                    if (e.Key == ui::KeyCode::A)
                    {
                        m_owner->SelectAll();
                        e.Handled = true;
                        return;
                    }
                }
                ui::ListView::OnKeyDown(e);
            }

        private:
            LogView* m_owner;
        };

        void ShowContextMenu(f32 localX, f32 localY)
        {
            if (Context == nullptr)
            {
                return;
            }
            auto menu = MakeRef<ui::ContextMenu>(MemoryAllocator());
            const usize count = SelectedCount();
            const String copyLabel =
                count > 1 ? Format(u8"Copy {} Lines", count) : String(u8"Copy");
            menu->AddItem(copyLabel.AsView(), [this]() { CopySelection(); }, count > 0);
            menu->AddItem(u8"Select All", [this]() { SelectAll(); }, !m_filtered.IsEmpty());
            menu->AddSeparator();
            menu->AddItem(u8"Clear", [this]() { Clear(); });
            const Float2 at = m_list->LocalToScreen(Float2{localX, localY});
            menu->Show(Context, at.x, at.y);
        }

        struct Entry
        {
            Bucket bucket = Bucket::Info;
            String text;
        };

        [[nodiscard]] static Color BucketColor(Bucket bucket) noexcept
        {
            switch (bucket)
            {
            case Bucket::Debug:
                return Color{150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f};
            case Bucket::Info:
                return Color{80.0f / 255.0f, 180.0f / 255.0f, 255.0f / 255.0f, 1.0f};
            case Bucket::Warning:
                return Color{255.0f / 255.0f, 200.0f / 255.0f, 50.0f / 255.0f, 1.0f};
            default:
                return Color{255.0f / 255.0f, 80.0f / 255.0f, 80.0f / 255.0f, 1.0f};
            }
        }

        // Recycled row = one Label; bind sets text + level color.
        class Adapter final : public ui::ListAdapterBase
        {
        public:
            explicit Adapter(LogView& owner) : m_owner(&owner) {}

            [[nodiscard]] i32 ItemCount() const override
            {
                return static_cast<i32>(m_owner->m_filtered.Size());
            }

            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                auto label = MakeRef<ui::Label>(m_owner->MemoryAllocator());
                label->FontSize.SetValue(12.0f);
                return RefPtr<ui::View>(label.Get());
            }

            void BindView(ui::View* view, i32 position) override
            {
                auto* label = Cast<ui::Label>(view);
                if (label == nullptr)
                {
                    return;
                }
                const usize index = m_owner->m_filtered[static_cast<usize>(position)];
                const Entry& entry = m_owner->m_entries[index];
                label->SetText(entry.text.AsView());
                label->TextColor.SetValue(Optional<Color>(BucketColor(entry.bucket)));
            }

        private:
            LogView* m_owner;
        };

        void RebuildFilter()
        {
            m_filtered.Clear();
            for (usize i = 0; i < m_entries.Size(); ++i)
            {
                if (IsBucketVisible(m_entries[i].bucket))
                {
                    m_filtered.PushBack(i);
                }
            }
            m_adapter->NotifyDataSetChanged();
        }

        void ScrollToNewest()
        {
            if (AutoScroll && !m_filtered.IsEmpty())
            {
                m_list->ScrollToPosition(static_cast<i32>(m_filtered.Size()) - 1);
            }
        }

        Array<Entry> m_entries;
        Array<usize> m_filtered; // indices into m_entries passing the filter
        bool m_visible[kBucketCount] = {true, true, true, true};

        UniquePtr<Adapter> m_adapter;
        RefPtr<EntryList> m_list;
        ui::CheckBox* m_filterBoxes[kBucketCount] = {}; // borrowed (toolbar owns them)
    };

    RTTI_DEFINE_OBJECT(LogView, "rtti::editor::editor::app")
}
