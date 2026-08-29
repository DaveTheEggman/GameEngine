// UI - :selection_model partition
//
// Decoupled selection state: tracks selected indices independently of the data view (multiple views can
// share one). Ported from Sedulous.UI/src/Data/SelectionModel.bf. Self-contained (no View). HashSet<i32>
// backing; Beef `mSelected.Add` -> HashSet::Insert (both return true if newly added); Math.Min/Max ->
// core::Min/Max.

module;
#include "Core/Prelude.h"

export module foundation.ui:selection_model;

import foundation.core; // HashSet, Array, Event? (Event is a UI partition)
import :event;

using namespace foundation::core;

export namespace foundation::ui
{
    enum class SelectionMode
    {
        None,
        Single,
        Multiple
    };

    class SelectionModel
    {
    public:
        SelectionMode Mode = SelectionMode::Single;

        Event<void()> OnSelectionChanged;

        [[nodiscard]] usize SelectedCount() const noexcept { return m_selected.Size(); }
        [[nodiscard]] bool IsSelected(i32 index) const noexcept
        {
            return m_selected.Contains(index);
        }

        /// Select an index EXCLUSIVELY (a plain click): the previous selection clears in every
        /// mode - extending is what Toggle (Ctrl) and SelectRange (Shift) are for. (Multiple
        /// mode must not accumulate here, or plain clicks would grow the selection forever.)
        void Select(i32 index)
        {
            if (Mode == SelectionMode::None)
            {
                return;
            }
            if (m_selected.Size() == 1 && m_selected.Contains(index))
            {
                return;
            } // already exactly this
            m_selected.Clear();
            m_selected.Insert(index);
            OnSelectionChanged.Invoke();
        }

        /// Deselect an index.
        void Deselect(i32 index)
        {
            if (m_selected.Remove(index))
            {
                OnSelectionChanged.Invoke();
            }
        }

        /// Drop selected indices that no longer exist (>= count, or negative). Called by list
        /// views when the data set changes - a stale index would silently highlight whatever row
        /// now occupies that position.
        void PruneFrom(i32 count)
        {
            Array<i32> stale;
            for (i32 index : m_selected)
            {
                if (index < 0 || index >= count)
                {
                    stale.PushBack(index);
                }
            }
            if (stale.IsEmpty())
            {
                return;
            }
            for (i32 index : stale)
            {
                m_selected.Remove(index);
            }
            OnSelectionChanged.Invoke();
        }

        /// Replace the whole selection in one step (one change event). Used to remap positional
        /// selection after the data set shifts (tree expand/collapse).
        void ReplaceAll(Span<const i32> indices)
        {
            m_selected.Clear();
            for (i32 index : indices)
            {
                if (index >= 0)
                {
                    m_selected.Insert(index);
                }
            }
            OnSelectionChanged.Invoke();
        }

        /// Toggle selection of an index (Ctrl+click).
        void Toggle(i32 index)
        {
            if (Mode == SelectionMode::None)
            {
                return;
            }
            if (IsSelected(index))
            {
                Deselect(index);
            }
            else
            {
                if (Mode == SelectionMode::Single)
                {
                    m_selected.Clear();
                }
                if (m_selected.Insert(index))
                {
                    OnSelectionChanged.Invoke();
                }
            }
        }

        /// Select the inclusive range from..to (Shift+click). Single mode selects only `to`.
        void SelectRange(i32 from, i32 to)
        {
            if (Mode == SelectionMode::None)
            {
                return;
            }
            if (Mode == SelectionMode::Single)
            {
                Select(to);
                return;
            }
            const i32 lo = Min(from, to);
            const i32 hi = Max(from, to);
            m_selected.Clear();
            for (i32 i = lo; i <= hi; ++i)
            {
                m_selected.Insert(i);
            }
            OnSelectionChanged.Invoke();
        }

        /// Clear all selections.
        void ClearSelection()
        {
            if (m_selected.Size() > 0)
            {
                m_selected.Clear();
                OnSelectionChanged.Invoke();
            }
        }

        /// The set of selected indices (borrowed).
        [[nodiscard]] const HashSet<i32>& SelectedPositions() const noexcept { return m_selected; }

        /// The first selected index, or -1.
        [[nodiscard]] i32 FirstSelected() const
        {
            // Iterator form rather than a range-for whose body always returns: the loop's
            // increment is then provably unreachable, which MSVC reports as C4702.
            const auto it = m_selected.begin();
            return (it != m_selected.end()) ? *it : -1;
        }

        /// Adjust indices when items are inserted/removed at/above startPos. delta > 0 = insertion, < 0 = removal.
        void ShiftIndices(i32 startPos, i32 delta)
        {
            Array<i32> old;
            for (i32 idx : m_selected)
            {
                old.PushBack(idx);
            }
            m_selected.Clear();
            for (i32 idx : old)
            {
                if (idx >= startPos)
                {
                    const i32 newIdx = idx + delta;
                    if (newIdx >= 0)
                    {
                        m_selected.Insert(newIdx);
                    }
                }
                else
                {
                    m_selected.Insert(idx);
                }
            }
        }

    private:
        HashSet<i32> m_selected;
    };
}
