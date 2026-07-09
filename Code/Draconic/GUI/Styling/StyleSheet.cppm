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
        // Set/override a property. An !important value resists later non-important overrides.
        void Set(core::StringView name, core::StringView value, bool important = false)
        {
            for (StyleProperty& p : m_props)
                if (p.Name == name)
                {
                    if (p.Important && !important) return; // important wins over normal
                    p.Value = value; p.Important = important;
                    return;
                }
            m_props.PushBack(StyleProperty(name, value, important));
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

        // Substitute whole-value var(--name[, fallback]) references against the resolved
        // custom properties (one level; nested/partial var() deferred).
        void ResolveVariables()
        {
            for (StyleProperty& p : m_props)
            {
                if (IsCustomProperty(p.Name.AsView())) continue;
                const core::StringView v = core::Trim(p.Value.AsView());
                if (v.Size() < 5 || v.SubStr(0, 4) != core::StringView(u8"var(") || v[v.Size() - 1] != u8')') continue;

                const core::StringView inside = v.SubStr(4, v.Size() - 5);
                usize comma = inside.Size();
                for (usize i = 0; i < inside.Size(); ++i) if (inside[i] == u8',') { comma = i; break; }

                const core::StringView varName = core::Trim(inside.SubStr(0, comma));
                const core::StringView fallback = (comma < inside.Size())
                    ? core::Trim(inside.SubStr(comma + 1, inside.Size() - comma - 1)) : core::StringView{};
                p.Value = Get(varName, fallback); // Get scans a different element; safe to assign here
            }
        }

    private:
        [[nodiscard]] static bool IsCustomProperty(core::StringView name) noexcept
        {
            return name.Size() >= 2 && name[0] == u8'-' && name[1] == u8'-';
        }

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

            // Apply low-to-high; later Set() overrides earlier (!important resists).
            ResolvedStyle out;
            for (const usize idx : matches)
                for (const StyleProperty& p : m_rules[idx].Properties())
                    out.Set(p.Name.AsView(), p.Value.AsView(), p.Important);
            out.ResolveVariables();
            return out;
        }

    private:
        Array<StyleRule> m_rules;
    };
}
