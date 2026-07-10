// Draconic UI Toolkit - :draggable_tree_view partition
//
// A TreeView with drag-to-reorder support. Ported from Sedulous.UI.Toolkit/src/DraggableTreeView.bf.
// Three public types in one partition (mirroring the Beef file): IReorderableTreeAdapter (extends
// ITreeAdapter with CanMove/MoveItem), TreeDragData (DragData payload carrying the source flat position),
// and DraggableTreeView (wraps an owned TreeView as a visual child and implements IDragSource/IDropTarget).
//
// Ownership adaptation (Beef `TreeView mTreeView ~ delete _`, owned but NOT in the child list) -> hold
// the TreeView as a RefPtr member exposed via VisualChildCount()/GetVisualChild() (the ScrollView/TreeView
// visual-child pattern). UIContext::AttachView/DetachView recurses VisualChildCount, so the internal
// TreeView attaches/detaches automatically; Parent is wired in the ctor. The adapter is BORROWED (raw
// pattern-B pointer; the consumer owns it).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui.toolkit:draggable_tree_view;

import draconic.core;
import draconic.vg;
import draconic.ui;

using namespace draconic::core;

export namespace draconic::ui::toolkit
{
    // ============================================================================================
    // IReorderableTreeAdapter - tree adapter that supports drag-to-reorder.
    // ============================================================================================
    class IReorderableTreeAdapter : public ITreeAdapter
    {
    public:
        /// Whether the item at fromPosition can be moved to toPosition.
        [[nodiscard]] virtual bool CanMove(i32 fromPosition, i32 toPosition) = 0;

        /// Move an item from one flat position to another.
        virtual void MoveItem(i32 fromPosition, i32 toPosition) = 0;
    };

    // ============================================================================================
    // TreeDragData - drag data for tree item reordering.
    // ============================================================================================
    class TreeDragData : public DragData
    {
        DRACONIC_OBJECT(TreeDragData, DragData)
    public:
        explicit TreeDragData(i32 sourcePosition)
            : DragData(u8"tree/reorder"), SourcePosition(sourcePosition) {}

        i32 SourcePosition = 0;
    };

    // ============================================================================================
    // DraggableTreeView - TreeView with drag-to-reorder support (IDragSource + IDropTarget).
    // ============================================================================================
    class DraggableTreeView : public ViewGroup, public IDragSource, public IDropTarget
    {
        DRACONIC_OBJECT(DraggableTreeView, ViewGroup)
    public:
        Event<void(DraggableTreeView*, i32, i32)> OnItemReordered;

        DraggableTreeView()
        {
            m_treeView = MakeRef<TreeView>(DefaultAllocator());
            m_treeView->Parent = this;
        }

        [[nodiscard]] bool DragEnabled() const { return m_dragEnabled; }
        void SetDragEnabled(bool value) { m_dragEnabled = value; }

        [[nodiscard]] TreeView* InternalTreeView() { return m_treeView.Get(); }
        [[nodiscard]] SelectionModel& Selection() { return m_treeView->Selection(); }

        [[nodiscard]] f32 ItemHeight() const { return m_treeView->ItemHeight(); }
        void SetItemHeight(f32 value) { m_treeView->SetItemHeight(value); }

        void SetAdapter(IReorderableTreeAdapter* adapter)
        {
            m_adapter = adapter;
            m_treeView->SetAdapter(adapter);
        }

        // === Visual children ===

        [[nodiscard]] usize VisualChildCount() const override { return 1; }
        [[nodiscard]] View* GetVisualChild(usize index) const override { return (index == 0) ? m_treeView.Get() : nullptr; }

        // === Drawing ===

        void OnDraw(UIDrawContext& ctx) override
        {
            DrawChildren(ctx);

            // Drop indicator line.
            if (m_dropIndicatorPos >= 0)
            {
                const Color indicatorColor = ResolveStyleColor(StyleProperty::AccentColor, Rgb(80, 160, 255, 255));
                const f32 y = m_dropIndicatorPos * m_treeView->ItemHeight();
                ctx.VG().FillRect(Rectangle{ 0, y, Width(), 2 }, indicatorColor);
            }
        }

        // === IDragSource ===

        [[nodiscard]] IDragSource* AsDragSource() override { return this; }

        [[nodiscard]] RefPtr<DragData> CreateDragData() override
        {
            if (!m_dragEnabled) { return RefPtr<DragData>{}; }
            const i32 sel = m_treeView->Selection().FirstSelected();
            if (sel < 0) { return RefPtr<DragData>{}; }
            return MakeRef<TreeDragData>(DefaultAllocator(), sel);
        }

        [[nodiscard]] RefPtr<View> CreateDragVisual(DragData* data) override
        {
            (void)data;
            RefPtr<Label> label = MakeRef<Label>(DefaultAllocator());
            label->SetText(u8"Moving item");
            return label;
        }

        void OnDragStarted(DragData* data) override { (void)data; }

        void OnDragCompleted(DragData* data, DragDropEffects effect, bool cancelled) override
        {
            (void)data;
            (void)effect;
            (void)cancelled;
            m_dropIndicatorPos = -1;
        }

        // === IDropTarget ===

        [[nodiscard]] IDropTarget* AsDropTarget() override { return this; }

        [[nodiscard]] DragDropEffects CanAcceptDrop(DragData* data, f32 localX, f32 localY) override
        {
            (void)localX;
            if (data->Format() != u8"tree/reorder") { return DragDropEffects::None; }

            if (auto* treeDrag = Cast<TreeDragData>(data))
            {
                const i32 targetPos = static_cast<i32>(localY / m_treeView->ItemHeight());
                if (m_adapter != nullptr && m_adapter->CanMove(treeDrag->SourcePosition, targetPos))
                {
                    return DragDropEffects::Move;
                }
            }
            return DragDropEffects::None;
        }

        void OnDragEnter(DragData* data, f32 localX, f32 localY) override
        {
            (void)data;
            (void)localX;
            UpdateDropIndicator(localY);
        }

        void OnDragOver(DragData* data, f32 localX, f32 localY) override
        {
            (void)data;
            (void)localX;
            UpdateDropIndicator(localY);
        }

        void OnDragLeave(DragData* data) override
        {
            (void)data;
            m_dropIndicatorPos = -1;
        }

        [[nodiscard]] DragDropEffects OnDrop(DragData* data, f32 localX, f32 localY) override
        {
            (void)localX;
            m_dropIndicatorPos = -1;

            if (auto* treeDrag = Cast<TreeDragData>(data))
            {
                const i32 targetPos = static_cast<i32>(localY / m_treeView->ItemHeight());
                if (m_adapter != nullptr && m_adapter->CanMove(treeDrag->SourcePosition, targetPos))
                {
                    m_adapter->MoveItem(treeDrag->SourcePosition, targetPos);
                    OnItemReordered.Invoke(this, treeDrag->SourcePosition, targetPos);
                    return DragDropEffects::Move;
                }
            }
            return DragDropEffects::None;
        }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            m_treeView->Measure(constraints);
            MeasuredSize = m_treeView->MeasuredSize;
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            m_treeView->Layout(0, 0, width, height);
        }

    private:
        [[nodiscard]] static Color Rgb(u8 r, u8 g, u8 b, u8 a = 255) noexcept
        {
            return Color{ r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
        }

        void UpdateDropIndicator(f32 localY)
        {
            m_dropIndicatorPos = static_cast<i32>(localY / m_treeView->ItemHeight());
        }

        RefPtr<TreeView> m_treeView;              // owned; visual child (not in the logical child list)
        IReorderableTreeAdapter* m_adapter = nullptr; // borrowed (consumer owns)
        bool m_dragEnabled = true;
        i32 m_dropIndicatorPos = -1;
    };

    DRACONIC_DEFINE_OBJECT(TreeDragData, "draconic::ui::toolkit")
    DRACONIC_DEFINE_OBJECT(DraggableTreeView, "draconic::ui::toolkit")
}
