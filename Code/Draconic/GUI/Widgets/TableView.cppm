// Draconic GUI - :table_view partition
//
// TableView: a virtualized, multi-column model-backed table. Modeled on eepp's
// UITableView/UIAbstractTableView (role only). Like ListView it realizes only the rows visible
// in its viewport (recycling a small pool), but each row holds one cell per column, and a fixed
// header row shows the column names (and reports header clicks, which a SortingProxyModel uses
// to sort). Single row selection (mouse + keyboard), wheel + scrollbar scrolling.
//
// Column widths: an explicit width per column (SetColumnWidth) or an equal split of the body
// width for the columns left auto. Horizontal scroll, column resize/reorder, and multi-selection
// are follow-ups.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:table_view;

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
    // A clickable column header cell (reports its column index on click).
    class TableHeaderCell : public Label
    {
        DRACONIC_OBJECT(TableHeaderCell, Label)
    public:
        TableHeaderCell()
        {
            SetTag(core::StringView(u8"tableheadercell"));
            SetTextAlignment(TextHAlign::Left, TextVAlign::Middle);
            SetPadding(Thickness{ 8.0f, 0.0f, 8.0f, 0.0f });
        }
        void SetColumn(usize column) noexcept { m_column = column; }
        void SetOnClicked(core::Function<void(usize)> callback) { m_onClicked = core::Move(callback); }
    protected:
        void OnMouseClick(const MouseEvent&) override { if (m_onClicked) m_onClicked(m_column); }
    private:
        usize m_column = 0;
        core::Function<void(usize)> m_onClicked;
    };

    // A recycled body row: one cell Label per column, plus a selection highlight. Cells are
    // hit-transparent so clicks reach the row (which reports its index).
    class TableRow : public UIWidget
    {
        DRACONIC_OBJECT(TableRow, UIWidget)
    public:
        TableRow() { SetTag(core::StringView(u8"tablerow")); }

        void SetRowIndex(i32 row) noexcept { m_row = row; }
        void SetSelected(bool selected) { if (m_selected != selected) { m_selected = selected; Invalidate(); } }
        void SetSelectionColor(Color color) { m_selectionColor = color; Invalidate(); }
        void SetOnPicked(core::Function<void(i32)> callback) { m_onPicked = core::Move(callback); }

        [[nodiscard]] usize CellCount() const noexcept { return m_cells.Size(); }
        [[nodiscard]] Label* CellAt(usize i) const { return i < m_cells.Size() ? m_cells[i].Get() : nullptr; }
        void EnsureCells(usize count, fonts::CachedFont* font, Color textColor)
        {
            while (m_cells.Size() < count)
            {
                auto cell = core::MakeRef<Label>(core::DefaultAllocator());
                cell->SetTag(core::StringView(u8"tablecell")); // container-owned, not a generic label
                cell->SetTextAlignment(TextHAlign::Left, TextVAlign::Middle);
                cell->SetPadding(Thickness{ 8.0f, 0.0f, 8.0f, 0.0f });
                cell->SetFont(font);
                cell->SetTextColor(textColor);
                cell->SetHitTestVisible(false); // clicks fall through to the row
                AddChild(cell.Get());
                m_cells.PushBack(core::Move(cell));
            }
        }

    protected:
        void OnMouseClick(const MouseEvent&) override { if (m_onPicked) m_onPicked(m_row); }
        void OnDraw(DrawContext& ctx, const Rect&) override
        {
            if (m_selected) ctx.VG().FillRect(GetLocalBounds().ToRectangle(), m_selectionColor);
        }

    private:
        i32 m_row = -1;
        bool m_selected = false;
        Color m_selectionColor{ 0.18f, 0.37f, 0.62f, 1.0f };
        Array<RefPtr<Label>> m_cells;
        core::Function<void(i32)> m_onPicked;
    };

    class TableView : public UIWidget, public IModelClient
    {
        DRACONIC_OBJECT(TableView, UIWidget)
    public:
        TableView()
        {
            SetTag(core::StringView(u8"tableview"));
            SetClipChildren(true);
            SetTabFocusable(true);
            SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), m_backgroundColor));

            m_header = core::MakeRef<UIWidget>(core::DefaultAllocator());
            m_header->SetTag(core::StringView(u8"tableheader"));
            m_header->SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), m_headerColor));
            AddChild(m_header.Get());

            m_vBar = core::MakeRef<ScrollBar>(core::DefaultAllocator());
            m_vBar->SetOrientation(Orientation::Vertical);
            TableView* self = this;
            m_vBar->SetOnValueChanged([self](f32 v) { if (!self->m_syncing) self->ScrollToFraction(v); });
            AddChild(m_vBar.Get());
        }

        ~TableView() override { if (m_model != nullptr) m_model->RemoveClient(this); }

        // === Model ===
        void SetModel(IModel* model)
        {
            if (m_model != nullptr) m_model->RemoveClient(this);
            m_model = model;
            if (m_model != nullptr) m_model->AddClient(this);
            m_selected = -1;
            m_offset = 0.0f;
            RebuildHeader();
            Relayout();
        }
        [[nodiscard]] IModel* GetModel() const noexcept { return m_model; }

        void OnModelUpdated() override
        {
            if (m_selected >= static_cast<i32>(RowCount())) m_selected = -1;
            if (m_headerCells.Size() != ColumnCount()) RebuildHeader();
            ClampOffset();
            Relayout();
        }

        // === Appearance ===
        void SetFont(fonts::CachedFont* font)
        {
            m_font = font;
            for (const RefPtr<TableHeaderCell>& h : m_headerCells) h->SetFont(font);
            for (const RefPtr<TableRow>& r : m_pool)
                for (usize c = 0; c < r->CellCount(); ++c) r->CellAt(c)->SetFont(font);
            Relayout();
        }
        void SetRowHeight(f32 height) { m_rowHeight = core::Max(1.0f, height); Relayout(); }
        void SetHeaderHeight(f32 height) { m_headerHeight = core::Max(0.0f, height); Relayout(); }
        void SetColumnWidth(usize column, f32 width)
        {
            while (m_columnWidths.Size() <= column) m_columnWidths.PushBack(0.0f);
            m_columnWidths[column] = width;
            Relayout();
        }
        void SetSelectionColor(Color color) { m_selectionColor = color; for (const RefPtr<TableRow>& r : m_pool) r->SetSelectionColor(color); }

        // `tableview { color }` propagates to the body cells (container-owned, like ListView);
        // header cells carry their own `tableheadercell` tag.
        void SetThemeTextColor(Color color) override
        {
            m_textColor = color;
            for (const RefPtr<TableRow>& r : m_pool)
                for (usize c = 0; c < r->CellCount(); ++c) r->CellAt(c)->SetTextColor(color);
        }
        void SetThemeFont(fonts::CachedFont* font) override { SetFont(font); }
        void SetThemePartColor(core::StringView part, Color color) override
        {
            if (part == core::StringView(u8"selection")) SetSelectionColor(color);
        }
        void CollectStyleParts(core::Array<core::StringView>& out) const override { out.PushBack(core::StringView(u8"selection")); }

        // === Selection ===
        void SetOnSelectionChanged(core::Function<void(ModelIndex)> callback) { m_onSelection = core::Move(callback); }
        [[nodiscard]] i32 GetSelectedRow() const noexcept { return m_selected; }
        [[nodiscard]] ModelIndex GetSelectedIndex() const noexcept { return m_selected >= 0 ? MakeModelIndex(m_selected) : ModelIndex{}; }
        void SetSelectedRow(i32 row) { SelectRow(row, /*notify*/ true, /*scrollIntoView*/ true); }

        // Header clicks (a column index) - a SortingProxyModel wires this to sort.
        void SetOnColumnHeaderClicked(core::Function<void(usize)> callback) { m_onHeaderClicked = core::Move(callback); }

        [[nodiscard]] usize VisibleRowCount() const noexcept { return m_visibleCount; }
        [[nodiscard]] f32 ScrollOffset() const noexcept { return m_offset; }

        [[nodiscard]] bool WantsWheel() const override { return true; }

    protected:
        void OnMouseWheel(const WheelEvent& event) override { ScrollBy(-event.Delta.y * m_rowHeight); }
        void OnKeyDown(const KeyEvent& event) override
        {
            const i32 rows = static_cast<i32>(RowCount());
            if (rows == 0) return;
            const i32 page = core::Max(1, static_cast<i32>(BodyHeight() / m_rowHeight) - 1);
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
        [[nodiscard]] usize ColumnCount() const { return m_model != nullptr ? m_model->ColumnCount() : 0; }
        [[nodiscard]] f32 BodyHeight() const { return core::Max(0.0f, GetSize().y - m_headerHeight); }
        [[nodiscard]] f32 ContentHeight() const { return static_cast<f32>(RowCount()) * m_rowHeight; }
        [[nodiscard]] f32 MaxScroll() const { return core::Max(0.0f, ContentHeight() - BodyHeight()); }
        void ClampOffset() { m_offset = core::Max(0.0f, core::Min(m_offset, MaxScroll())); }
        void ScrollBy(f32 dy) { m_offset += dy; ClampOffset(); Relayout(); }
        void ScrollToFraction(f32 fraction) { m_offset = fraction * MaxScroll(); ClampOffset(); Relayout(); }
        void ScrollRowIntoView(i32 row)
        {
            const f32 top = static_cast<f32>(row) * m_rowHeight;
            const f32 bottom = top + m_rowHeight;
            if (top < m_offset) m_offset = top;
            else if (bottom > m_offset + BodyHeight()) m_offset = bottom - BodyHeight();
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

        void RebuildHeader()
        {
            for (const RefPtr<TableHeaderCell>& h : m_headerCells) h->RemoveFromParent();
            m_headerCells.Clear();
            const usize columns = ColumnCount();
            TableView* self = this;
            for (usize c = 0; c < columns; ++c)
            {
                auto cell = core::MakeRef<TableHeaderCell>(core::DefaultAllocator());
                cell->SetColumn(c);
                cell->SetFont(m_font);
                cell->SetTextColor(m_headerTextColor);
                cell->SetText(m_model->ColumnName(c).AsView());
                cell->SetOnClicked([self](usize col) { if (self->m_onHeaderClicked) self->m_onHeaderClicked(col); });
                m_header->AddChild(cell.Get());
                m_headerCells.PushBack(core::Move(cell));
            }
        }

        // Per-column widths: explicit where set (>0), else an equal split of the remaining body
        // width among the auto columns.
        void ComputeColumnWidths(f32 bodyWidth, Array<f32>& out) const
        {
            const usize columns = ColumnCount();
            out.Clear();
            f32 explicitTotal = 0.0f;
            usize autoCount = 0;
            for (usize c = 0; c < columns; ++c)
            {
                const f32 w = c < m_columnWidths.Size() ? m_columnWidths[c] : 0.0f;
                if (w > 0.0f) explicitTotal += w; else ++autoCount;
            }
            const f32 autoWidth = autoCount > 0 ? core::Max(0.0f, (bodyWidth - explicitTotal) / static_cast<f32>(autoCount)) : 0.0f;
            for (usize c = 0; c < columns; ++c)
            {
                const f32 w = c < m_columnWidths.Size() ? m_columnWidths[c] : 0.0f;
                out.PushBack(w > 0.0f ? w : autoWidth);
            }
        }

        void EnsurePool(usize needed)
        {
            while (m_pool.Size() < needed)
            {
                auto row = core::MakeRef<TableRow>(core::DefaultAllocator());
                row->SetSelectionColor(m_selectionColor);
                TableView* self = this;
                row->SetOnPicked([self](i32 r) { self->SelectRow(r, /*notify*/ true, /*scrollIntoView*/ false); });
                AddChild(row.Get());
                m_pool.PushBack(core::Move(row));
            }
        }

        void Relayout()
        {
            const core::Float2 size = GetSize();
            const bool barVisible = MaxScroll() > 0.0f;
            const f32 bodyWidth = core::Max(0.0f, barVisible ? size.x - m_barThickness : size.x);

            Array<f32> widths;
            ComputeColumnWidths(bodyWidth, widths);
            const auto columnX = [&](usize c) { f32 x = 0.0f; for (usize i = 0; i < c && i < widths.Size(); ++i) x += widths[i]; return x; };

            // Header on top, full body width.
            m_header->SetVisible(m_headerHeight > 0.0f);
            m_header->SetPosition(core::Float2{ 0.0f, 0.0f });
            m_header->SetSize(core::Float2{ bodyWidth, m_headerHeight });
            for (usize c = 0; c < m_headerCells.Size(); ++c)
            {
                m_headerCells[c]->SetPosition(core::Float2{ columnX(c), 0.0f });
                m_headerCells[c]->SetSize(core::Float2{ c < widths.Size() ? widths[c] : 0.0f, m_headerHeight });
            }

            // Virtualized body rows.
            const i32 rows = static_cast<i32>(RowCount());
            const i32 first = m_rowHeight > 0.0f ? static_cast<i32>(m_offset / m_rowHeight) : 0;
            const i32 span = m_rowHeight > 0.0f ? static_cast<i32>(BodyHeight() / m_rowHeight) + 2 : 0;
            const i32 last = core::Min(rows, first + core::Max(0, span));
            const usize needed = static_cast<usize>(core::Max(0, last - first));
            EnsurePool(needed);
            m_visibleCount = needed;

            const usize columns = ColumnCount();
            usize p = 0;
            for (i32 r = first; r < last; ++r, ++p)
            {
                TableRow* row = m_pool[p].Get();
                row->SetVisible(true);
                row->SetRowIndex(r);
                row->SetSelected(r == m_selected);
                row->SetPosition(core::Float2{ 0.0f, m_headerHeight + static_cast<f32>(r) * m_rowHeight - m_offset });
                row->SetSize(core::Float2{ bodyWidth, m_rowHeight });
                row->EnsureCells(columns, m_font, m_textColor);
                for (usize c = 0; c < columns; ++c)
                {
                    Label* cell = row->CellAt(c);
                    cell->SetText(m_model->Data(MakeModelIndex(r, static_cast<i32>(c))).ToString().AsView());
                    cell->SetPosition(core::Float2{ columnX(c), 0.0f });
                    cell->SetSize(core::Float2{ c < widths.Size() ? widths[c] : 0.0f, m_rowHeight });
                }
            }
            for (; p < m_pool.Size(); ++p) m_pool[p]->SetVisible(false);

            m_header->ToFront(); // draw over rows scrolled up under it

            // Scrollbar overlay in the body region.
            if (barVisible)
            {
                m_vBar->SetVisible(true);
                m_vBar->SetPosition(core::Float2{ size.x - m_barThickness, m_headerHeight });
                m_vBar->SetSize(core::Float2{ m_barThickness, BodyHeight() });
                m_vBar->SetThumbProportion(ContentHeight() > 0.0f ? BodyHeight() / ContentHeight() : 1.0f);
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

        IModel* m_model = nullptr; // non-owning
        RefPtr<UIWidget> m_header;
        Array<RefPtr<TableHeaderCell>> m_headerCells;
        Array<RefPtr<TableRow>> m_pool;
        RefPtr<ScrollBar> m_vBar;
        Array<f32> m_columnWidths; // 0 = auto
        fonts::CachedFont* m_font = nullptr;
        core::Function<void(ModelIndex)> m_onSelection;
        core::Function<void(usize)> m_onHeaderClicked;
        i32 m_selected = -1;
        f32 m_offset = 0.0f;
        f32 m_rowHeight = 24.0f;
        f32 m_headerHeight = 26.0f;
        f32 m_barThickness = 12.0f;
        usize m_visibleCount = 0;
        bool m_syncing = false;
        Color m_backgroundColor{ 0.12f, 0.13f, 0.16f, 1.0f };
        Color m_headerColor{ 0.20f, 0.22f, 0.27f, 1.0f };
        Color m_headerTextColor{ 0.86f, 0.89f, 0.94f, 1.0f };
        Color m_textColor{ 0.86f, 0.89f, 0.94f, 1.0f };
        Color m_selectionColor{ 0.18f, 0.37f, 0.62f, 1.0f };
    };

    DRACONIC_DEFINE_OBJECT(TableHeaderCell, "draconic::gui")
    DRACONIC_DEFINE_OBJECT(TableRow, "draconic::gui")
    DRACONIC_DEFINE_OBJECT(TableView, "draconic::gui")
}
