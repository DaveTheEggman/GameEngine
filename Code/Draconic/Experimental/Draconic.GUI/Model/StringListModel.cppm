// Draconic GUI - :string_list_model partition
//
// StringListModel: the simplest concrete Model - a single column of strings, one per row.
// Modeled on eepp's Models::ItemListModel/StringMapModel role. Backs a ListView; mutating it
// (SetItems/AddItem/Clear) notifies attached views via DidUpdate.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.gui:string_list_model;

import draconic.core; // Array, String, StringView, Move
import :variant;
import :model_index;
import :model;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    class StringListModel : public IModel
    {
    public:
        StringListModel() = default;
        explicit StringListModel(Array<core::String> items) : m_items(core::Move(items)) {}

        void SetItems(Array<core::String> items)
        {
            m_items = core::Move(items);
            DidUpdate();
        }
        void AddItem(core::StringView item)
        {
            m_items.PushBack(core::String(item));
            DidUpdate();
        }
        void Clear()
        {
            m_items.Clear();
            DidUpdate();
        }

        [[nodiscard]] core::StringView ItemAt(usize row) const
        {
            return row < m_items.Size() ? m_items[row].AsView() : core::StringView{};
        }

        [[nodiscard]] usize RowCount(const ModelIndex& parent = {}) const override
        {
            return parent.IsValid() ? 0 : m_items.Size();
        }
        [[nodiscard]] usize ColumnCount() const override { return 1; }
        [[nodiscard]] Variant Data(const ModelIndex& index,
                                   ModelRole role = ModelRole::Display) const override
        {
            if (!IsValidIndex(index))
                return Variant{};
            if (role == ModelRole::Display || role == ModelRole::Sort)
                return Variant(m_items[static_cast<usize>(index.Row)].AsView());
            return Variant{};
        }

    private:
        Array<core::String> m_items;
    };
}
