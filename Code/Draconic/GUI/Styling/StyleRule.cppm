// Draconic GUI - :style_rule partition
//
// StyleProperty + StyleRule: a CSS declaration block. Ported from eepp's
// css/StyleSheetProperty + StyleSheetStyle. A StyleRule pairs one StyleSelector with a list
// of name/value declarations. Values stay as strings here; interpreting them into typed
// widget properties (color/length/drawable) is a later CSS increment.

module;
#include "Core/Prelude.h"

export module draconic.gui:style_rule;

import draconic.core;   // String, StringView, Array, i64, Move
import :style_selector;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    struct StyleProperty
    {
        core::String Name;
        core::String Value;

        StyleProperty() = default;
        StyleProperty(core::StringView name, core::StringView value) : Name(name), Value(value) {}
    };

    class StyleRule
    {
    public:
        StyleRule() = default;
        explicit StyleRule(StyleSelector selector) : m_selector(core::Move(selector)) {}
        explicit StyleRule(core::StringView selector) : m_selector(selector) {}

        [[nodiscard]] const StyleSelector& Selector() const noexcept { return m_selector; }
        [[nodiscard]] i64 Specificity() const noexcept { return m_selector.Specificity(); }

        // Set (or override) a declaration.
        void SetProperty(core::StringView name, core::StringView value)
        {
            for (StyleProperty& p : m_properties)
                if (p.Name == name) { p.Value = value; return; }
            m_properties.PushBack(StyleProperty(name, value));
        }

        [[nodiscard]] const Array<StyleProperty>& Properties() const noexcept { return m_properties; }
        [[nodiscard]] usize PropertyCount() const noexcept { return m_properties.Size(); }

    private:
        StyleSelector m_selector;
        Array<StyleProperty> m_properties;
    };
}
