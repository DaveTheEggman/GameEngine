// GUI - :style_rule partition
//
// StyleProperty + StyleRule: a CSS declaration block. Ported from eepp's
// css/StyleSheetProperty + StyleSheetStyle. A StyleRule pairs one StyleSelector with a list
// of name/value declarations. Values stay as strings here; interpreting them into typed
// widget properties (color/length/drawable) is a later CSS increment.

module;
#include "Core/Prelude.h"

export module experimental.gui:style_rule;

import foundation.core; // String, StringView, Array, i64, Move
import :style_selector;
import :media_query;

using namespace foundation::core;
namespace core = foundation::core;

export namespace experimental::gui
{
    struct StyleProperty
    {
        core::String Name;
        core::String Value;
        bool Important = false;

        StyleProperty() = default;
        StyleProperty(core::StringView name, core::StringView value, bool important = false)
            : Name(name), Value(value), Important(important)
        {
        }
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
        void SetProperty(core::StringView name, core::StringView value, bool important = false)
        {
            for (StyleProperty& p : m_properties)
                if (p.Name == name)
                {
                    p.Value = value;
                    p.Important = important;
                    return;
                }
            m_properties.PushBack(StyleProperty(name, value, important));
        }

        [[nodiscard]] const Array<StyleProperty>& Properties() const noexcept
        {
            return m_properties;
        }
        [[nodiscard]] usize PropertyCount() const noexcept { return m_properties.Size(); }

        // The @media condition this rule is nested in (empty = always active).
        void SetMedia(MediaQuery media) { m_media = core::Move(media); }
        [[nodiscard]] const MediaQuery& Media() const noexcept { return m_media; }

    private:
        StyleSelector m_selector;
        Array<StyleProperty> m_properties;
        MediaQuery m_media;
    };
}
