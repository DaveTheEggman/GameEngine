// Draconic GUI - :style_sheet partition
//
// ResolvedStyle + StyleSheet: the cascade. Ported from eepp's css/StyleSheet(::getElement
// Styles). A StyleSheet is an ordered list of StyleRules; Resolve(element) gathers the rules
// that match, orders them by (specificity, source order), and applies their declarations so
// higher-specificity and later-source rules win - producing a flat name/value ResolvedStyle.

module;
#include "Core/Prelude.h"

export module draconic.gui:style_sheet;

import draconic.core;   // String, StringView, Array, i64, Move
import :style_selector;
import :style_rule;
import :ui_widget;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    // The flat, cascaded property set for one element.
    class ResolvedStyle
    {
    public:
        void Set(core::StringView name, core::StringView value)
        {
            for (StyleProperty& p : m_props)
                if (p.Name == name) { p.Value = value; return; }
            m_props.PushBack(StyleProperty(name, value));
        }

        [[nodiscard]] bool Has(core::StringView name) const
        {
            for (const StyleProperty& p : m_props)
                if (p.Name == name) return true;
            return false;
        }

        [[nodiscard]] core::StringView Get(core::StringView name, core::StringView fallback = core::StringView{}) const
        {
            for (const StyleProperty& p : m_props)
                if (p.Name == name) return p.Value.AsView();
            return fallback;
        }

        [[nodiscard]] usize Count() const noexcept { return m_props.Size(); }
        [[nodiscard]] const Array<StyleProperty>& Properties() const noexcept { return m_props; }

    private:
        Array<StyleProperty> m_props;
    };

    class StyleSheet
    {
    public:
        void AddRule(StyleRule rule) { m_rules.PushBack(core::Move(rule)); }
        [[nodiscard]] usize RuleCount() const noexcept { return m_rules.Size(); }
        [[nodiscard]] const Array<StyleRule>& Rules() const noexcept { return m_rules; }

        [[nodiscard]] ResolvedStyle Resolve(const UIWidget& element, bool applyPseudo = true) const
        {
            // Collect indices of matching rules (in source order).
            Array<usize> matches;
            for (usize i = 0; i < m_rules.Size(); ++i)
                if (m_rules[i].Selector().Select(element, applyPseudo)) matches.PushBack(i);

            // Stable insertion sort by specificity (ties keep source order -> later wins on apply).
            for (usize a = 1; a < matches.Size(); ++a)
            {
                const usize key = matches[a];
                const i64 keySpec = m_rules[key].Specificity();
                usize b = a;
                while (b > 0 && m_rules[matches[b - 1]].Specificity() > keySpec) { matches[b] = matches[b - 1]; --b; }
                matches[b] = key;
            }

            // Apply low-to-high; later Set() overrides earlier.
            ResolvedStyle out;
            for (const usize idx : matches)
                for (const StyleProperty& p : m_rules[idx].Properties())
                    out.Set(p.Name.AsView(), p.Value.AsView());
            return out;
        }

    private:
        Array<StyleRule> m_rules;
    };
}
