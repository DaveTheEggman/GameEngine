// Draconic GUI - :list_view partition
//
// ListView: a virtualized, model-backed single-column list. Modeled on eepp's
// UIListView/UIAbstractView (role only). Unlike ListBox (which builds one child per item),
// ListView renders only the rows visible in its viewport, recycling a small pool of row
// widgets as it scrolls - so a 100k-row model costs a handful of widgets. It reads its data
// from an IModel (and re-reads on the model's DidUpdate, being an IModelClient), supports
// single selection (mouse + keyboard), wheel + scrollbar scrolling, and scroll-into-view.
//
// Multi-selection, columns/TableView, and tree nesting are follow-ups on the same MVC core.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:list_view;

import draconic.core;   // RefPtr, MakeRef, Array, Function, Move, Max, Min, Float2
import draconic.fonts;  // CachedFont
import :rect;
import :event;
import :draw_context;
import :rectangle_drawable;
import :node;
import :label;
import :ui_widget;
import :scroll_bar;
import :linear_layout;  // Orientation
import :variant;
import :model_index;
import :model;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

export namespace draconic::gui
{
    // One recycled row: a Label that highlights when selected and reports clicks by row index.
    class ListViewRow : public Label
    {
        DRACONIC_OBJECT(ListViewRow, Label)
    public:
        ListViewRow()
        {
            SetTag(core::StringView(u8"listviewrow"));
            SetTextAlignment(TextHAlign::Left, TextVAlign::Middle);
            SetPadding(Thickness{ 8.0f, 0.0f, 8.0f, 0.0f });
        }

        void SetRowIndex(i32 row) noexcept { m_row = row; }
        [[nodiscard]] i32 RowIndex() const noexcept { return m_row; }
        void SetSelected(bool selected) { if (m_selected != selected) { m_selected = selected; Invalidate(); } }
        [[nodiscard]] bool IsSelected() const noexcept { return m_selected; }
        void SetSelectionColor(Color color) { m_selectionColor = color; Invalidate(); }
        void SetOnPicked(core::Function<void(i32)> callback) { m_onPicked = core::Move(callback); }

    protected:
        void OnMouseClick(const MouseEvent&) override { if (m_onPicked) m_onPicked(m_row); }

        void OnDraw(DrawContext& ctx, const Rect& localBounds) override
        {
            if (m_selected)
                ctx.VG().FillRect(GetLocalBounds().ToRectangle(), m_selectionColor);
            Label::OnDraw(ctx, localBounds);
        }

    private:
        i32 m_row = -1;
        bool m_selected = false;
        Color m_selectionColor{ 0.18f, 0.37f, 0.62f, 1.0f };
        core::Function<void(i32)> m_onPicked;
    };

    class ListView : public UIWidget, public IModelClient
    {
        DRACONIC_OBJECT(ListView, UIWidget)
    public:
        ListView()
        {
            SetTag(core::StringView(u8"listview"));
            SetClipChildren(true);
            SetTabFocusable(true);
            SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), m_backgroundColor));

            m_vBar = core::MakeRef<ScrollBar>(core::DefaultAllocator());
            m_vBar->SetOrientation(Orientation::Vertical);
            ListView* self = this;
            m_vBar->SetOnValueChanged([self](f32 v) { if (!self->m_syncing) self->ScrollToFraction(v); });
            AddChild(m_vBar.Get());
        }

        ~ListView() override { if (m_model != nullptr) m_model->RemoveClient(this); }

        // === Model ===
        void SetModel(IModel* model)
        {
            if (m_model != nullptr) m_model->RemoveClient(this);
            m_model = model;
            if (m_model != nullptr) m_model->AddClient(this);
            m_selected = -1;
            m_offset = 0.0f;
            Relayout();
        }
        [[nodiscard]] IModel* GetModel() const noexcept { return m_model; }

        void OnModelUpdated() override
        {
            if (m_selected >= static_cast<i32>(RowCount())) m_selected = -1;
            ClampOffset();
            Relayout();
        }

        // === Appearance ===
        void SetFont(fonts::CachedFont* font) { m_font = font; for (const RefPtr<ListViewRow>& r : m_pool) r->SetFont(font); Relayout(); }
        void SetRowHeight(f32 height) { m_rowHeight = core::Max(1.0f, height); Relayout(); }
        [[nodiscard]] f32 GetRowHeight() const noexcept { return m_rowHeight; }
        void SetRowTextColor(Color color) { m_textColor = color; for (const RefPtr<ListViewRow>& r : m_pool) r->SetTextColor(color); }
        void SetSelectionColor(Color color) { m_selectionColor = color; for (const RefPtr<ListViewRow>& r : m_pool) r->SetSelectionColor(color); }

        void SetThemeTextColor(Color color) override { SetRowTextColor(color); }
        void SetThemeFont(fonts::CachedFont* font) override { SetFont(font); }
        void CollectStyleParts(core::Array<core::StringView>& out) const override { out.PushBack(core::StringView(u8"selection")); }
        void SetThemePartColor(core::StringView part, Color color) override
        {
            if (part == core::StringView(u8"selection")) SetSelectionColor(color);
        }

        // === Selection ===
        void SetOnSelectionChanged(core::Function<void(ModelIndex)> callback) { m_onSelection = core::Move(callback); }
        [[nodiscard]] i32 GetSelectedRow() const noexcept { return m_selected; }
        [[nodiscard]] ModelIndex GetSelectedIndex() const noexcept { return m_selected >= 0 ? MakeModelIndex(m_selected) : ModelIndex{}; }
        void SetSelectedRow(i32 row) { SelectRow(row, /*notify*/ true, /*scrollIntoView*/ true); }

        // Number of row widgets currently realized as visible (for tests/inspection).
        [[nodiscard]] usize VisibleRowCount() const noexcept { return m_visibleCount; }
        [[nodiscard]] f32 ScrollOffset() const noexcept { return m_offset; }

        [[nodiscard]] bool WantsWheel() const override { return true; }

    protected:
        void OnMouseWheel(const WheelEvent& event) override { ScrollBy(-event.Delta.y * m_rowHeight * 1.0f); }

        void OnKeyDown(const KeyEvent& event) override
        {
            const i32 rows = static_cast<i32>(RowCount());
            if (rows == 0) return;
            const i32 page = core::Max(1, static_cast<i32>(GetSize().y / m_rowHeight) - 1);
            switch (static_cast<KeyCode>(event.KeyCode))
            {
            case KeyCode::Up:       SelectRow(m_selected <= 0 ? 0 : m_selected - 1, true, true); break;
            case KeyCode::Down:     SelectRow(m_selected < 0 ? 0 : core::Min(rows - 1, m_selected + 1), true, true); break;
            case KeyCode::Home:     SelectRow(0, true, true); break;
            case KeyCode::End:      SelectRow(rows - 1, true, true); break;
            case KeyCode::PageUp:   SelectRow(core::Max(0, (m_selected < 0 ? 0 : m_selected) - page), true, true); break;
            case KeyCode::PageDown: SelectRow(core::Min(rows - 1, (m_selected < 0 ? 0 : m_selected) + page), true, true); break;
            default: return;
            }
        }

        void OnSizeChange() override { Relayout(); }

    private:
        [[nodiscard]] usize RowCount() const { return m_model != nullptr ? m_model->RowCount() : 0; }
        [[nodiscard]] f32 ContentHeight() const { return static_cast<f32>(RowCount()) * m_rowHeight; }
        [[nodiscard]] f32 MaxScroll() const { return core::Max(0.0f, ContentHeight() - GetSize().y); }
        void ClampOffset() { m_offset = core::Max(0.0f, core::Min(m_offset, MaxScroll())); }

        void ScrollBy(f32 dy) { m_offset += dy; ClampOffset(); Relayout(); }
        void ScrollToFraction(f32 fraction) { m_offset = fraction * MaxScroll(); ClampOffset(); Relayout(); }
        void ScrollRowIntoView(i32 row)
        {
            const f32 top = static_cast<f32>(row) * m_rowHeight;
            const f32 bottom = top + m_rowHeight;
            if (top < m_offset) m_offset = top;
            else if (bottom > m_offset + GetSize().y) m_offset = bottom - GetSize().y;
            ClampOffset();
        }

        void SelectRow(i32 row, bool notify, bool scrollIntoView)
        {
            const i32 rows = static_cast<i32>(RowCount());
            if (row < 0 || row >= rows) return;
            m_selected = row;
            if (scrollIntoView) ScrollRowIntoView(row);
            Relayout();
            if (notify && m_onSelection) m_onSelection(GetSelectedIndex());
        }

        void EnsurePool(usize needed)
        {
            while (m_pool.Size() < needed)
            {
                auto row = core::MakeRef<ListViewRow>(core::DefaultAllocator());
                row->SetFont(m_font);
                row->SetTextColor(m_textColor);
                row->SetSelectionColor(m_selectionColor);
                ListView* self = this;
                row->SetOnPicked([self](i32 r) { self->SelectRow(r, /*notify*/ true, /*scrollIntoView*/ false); });
                AddChild(row.Get());
                m_pool.PushBack(core::Move(row));
            }
        }

        void Relayout()
        {
            const core::Float2 size = GetSize();
            const bool barVisible = MaxScroll() > 0.0f;
            const f32 contentWidth = core::Max(0.0f, barVisible ? size.x - m_barThickness : size.x);

            // Visible row range (+1 buffer row for partial rows at the edges).
            const i32 rows = static_cast<i32>(RowCount());
            const i32 first = m_rowHeight > 0.0f ? static_cast<i32>(m_offset / m_rowHeight) : 0;
            const i32 span = m_rowHeight > 0.0f ? static_cast<i32>(size.y / m_rowHeight) + 2 : 0;
            const i32 last = core::Min(rows, first + core::Max(0, span));
            const usize needed = static_cast<usize>(core::Max(0, last - first));

            EnsurePool(needed);
            m_visibleCount = needed;

            usize p = 0;
            for (i32 r = first; r < last; ++r, ++p)
            {
                ListViewRow* row = m_pool[p].Get();
                row->SetVisible(true);
                row->SetRowIndex(r);
                row->SetText(m_model->Data(MakeModelIndex(r)).ToString().AsView());
                row->SetSelected(r == m_selected);
                row->SetPosition(core::Float2{ 0.0f, static_cast<f32>(r) * m_rowHeight - m_offset });
                row->SetSize(core::Float2{ contentWidth, m_rowHeight });
            }
            for (; p < m_pool.Size(); ++p) m_pool[p]->SetVisible(false);

            // Scrollbar overlay.
            if (barVisible)
            {
                m_vBar->SetVisible(true);
                m_vBar->SetPosition(core::Float2{ size.x - m_barThickness, 0.0f });
                m_vBar->SetSize(core::Float2{ m_barThickness, size.y });
                m_vBar->SetThumbProportion(ContentHeight() > 0.0f ? size.y / ContentHeight() : 1.0f);
                m_syncing = true;
                m_vBar->SetValue(MaxScroll() > 0.0f ? m_offset / MaxScroll() : 0.0f);
                m_syncing = false;
                m_vBar->ToFront();
            }
            else
            {
                m_vBar->SetVisible(false);
            }
        }

        IModel* m_model = nullptr;             // non-owning
        Array<RefPtr<ListViewRow>> m_pool;     // recycled row widgets
        RefPtr<ScrollBar> m_vBar;
        fonts::CachedFont* m_font = nullptr;
        core::Function<void(ModelIndex)> m_onSelection;
        i32 m_selected = -1;
        f32 m_offset = 0.0f;                   // vertical scroll in px
        f32 m_rowHeight = 24.0f;
        f32 m_barThickness = 12.0f;
        usize m_visibleCount = 0;
        bool m_syncing = false;                // guards scrollbar<->offset feedback
        Color m_backgroundColor{ 0.12f, 0.13f, 0.16f, 1.0f };
        Color m_textColor{ 0.88f, 0.90f, 0.94f, 1.0f };
        Color m_selectionColor{ 0.18f, 0.37f, 0.62f, 1.0f };
    };

    DRACONIC_DEFINE_OBJECT(ListViewRow, "draconic::gui")
    DRACONIC_DEFINE_OBJECT(ListView, "draconic::gui")
}
