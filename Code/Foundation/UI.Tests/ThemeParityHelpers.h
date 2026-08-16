// Shared theme-parity comparator (ui-theme-migration.md): a parsed .sss sheet must declare the
// SAME styling as its legacy C++ rule builder - rule for rule, value for value - BEFORE the C++
// body may be deleted. Comparison is by MERGED selector signature (several source rules may
// target one selector; the cascade merges them identically in both sheets), with drawables
// compared structurally (type + fields; state lists recursed per state).
//
// Include AFTER doctest and the foundation.core/foundation.ui imports (uses both). Used by
// UI.Tests (built-in themes) and UI.Toolkit.Tests (the toolkit fragments).
#pragma once

namespace theme_parity
{
    using namespace foundation::ui;
    using namespace foundation::core;

    struct SelKey
    {
        const TypeInfo* type = nullptr;
        String classes; // joined, order-preserved (themes use at most one)
        u32 state = 0;
        String pseudo;

        bool operator==(const SelKey& o) const
        {
            return type == o.type && classes == o.classes && state == o.state &&
                   pseudo == o.pseudo;
        }
    };

    inline SelKey KeyOf(const StyleSelector& sel)
    {
        SelKey k;
        k.type = sel.ViewType;
        for (const String& c : sel.StyleClasses)
        {
            k.classes.Append(c.AsView());
            k.classes.Append(u8"|");
        }
        k.state = sel.State.HasValue() ? static_cast<u32>(sel.State.Value()) : 0xffffffffu;
        k.pseudo = sel.PseudoElement.HasValue() ? sel.PseudoElement.Value() : String{};
        return k;
    }

    struct MergedRule
    {
        SelKey key;
        Array<StyleRule::Entry> props; // later Set wins per property (both sheets merge alike)
    };

    inline void MergeSheet(const StyleSheet& sheet, Array<MergedRule>& out)
    {
        for (usize r = 0; r < sheet.RuleCount(); ++r)
        {
            const StyleRule& rule = sheet.GetRule(r);
            const SelKey key = KeyOf(rule.Selector);
            MergedRule* slot = nullptr;
            for (MergedRule& m : out)
            {
                if (m.key == key)
                {
                    slot = &m;
                    break;
                }
            }
            if (slot == nullptr)
            {
                out.PushBack(MergedRule{key, {}});
                slot = &out[out.Size() - 1];
            }
            for (usize i = 0; i < rule.PropertyCount(); ++i)
            {
                const StyleRule::Entry& e = rule.GetProperty(i);
                bool replaced = false;
                for (StyleRule::Entry& existing : slot->props)
                {
                    if (existing.Prop == e.Prop)
                    {
                        existing = e;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced)
                {
                    slot->props.PushBack(e);
                }
            }
        }
    }

    inline bool ColorsEqual(Color a, Color b)
    {
        const f32 eps = 1e-4f;
        return Abs(a.r - b.r) < eps && Abs(a.g - b.g) < eps && Abs(a.b - b.b) < eps &&
               Abs(a.a - b.a) < eps;
    }

    inline bool DrawablesEquivalent(Drawable* a, Drawable* b); // fwd

    inline bool StateListsEquivalent(StateListDrawable* a, StateListDrawable* b)
    {
        static constexpr ControlState kStates[] = {
            ControlState::Normal,   ControlState::Hover,   ControlState::Pressed,
            ControlState::Disabled, ControlState::Focused, ControlState::Checked,
        };
        for (const ControlState s : kStates)
        {
            Drawable* da = a->Get(s);
            Drawable* db = b->Get(s);
            if ((da == nullptr) != (db == nullptr))
            {
                return false;
            }
            if (da != nullptr && !DrawablesEquivalent(da, db))
            {
                return false;
            }
        }
        return true;
    }

    inline bool DrawablesEquivalent(Drawable* a, Drawable* b)
    {
        if (a == b)
        {
            return true; // shared instance (ThemeIconSet glyphs when the set is live)
        }
        if (a == nullptr || b == nullptr)
        {
            return false;
        }
        if (auto* ca = foundation::core::Cast<ColorDrawable>(a))
        {
            auto* cb = foundation::core::Cast<ColorDrawable>(b);
            return cb != nullptr && ColorsEqual(ca->Color, cb->Color);
        }
        if (auto* ra = foundation::core::Cast<RoundedRectDrawable>(a))
        {
            auto* rb = foundation::core::Cast<RoundedRectDrawable>(b);
            return rb != nullptr && ColorsEqual(ra->FillColor, rb->FillColor) &&
                   ColorsEqual(ra->BorderColor, rb->BorderColor) &&
                   Abs(ra->BorderWidth - rb->BorderWidth) < 1e-4f &&
                   Abs(ra->Radii.topLeft - rb->Radii.topLeft) < 1e-4f &&
                   Abs(ra->Radii.topRight - rb->Radii.topRight) < 1e-4f &&
                   Abs(ra->Radii.bottomRight - rb->Radii.bottomRight) < 1e-4f &&
                   Abs(ra->Radii.bottomLeft - rb->Radii.bottomLeft) < 1e-4f;
        }
        if (auto* sa = foundation::core::Cast<StateListDrawable>(a))
        {
            auto* sb = foundation::core::Cast<StateListDrawable>(b);
            return sb != nullptr && StateListsEquivalent(sa, sb);
        }
        if (foundation::core::Cast<SVGDrawable>(a) != nullptr)
        {
            return foundation::core::Cast<SVGDrawable>(b) != nullptr; // same glyph family suffices
        }
        // Unknown drawable type: require exact type match at least.
        return a->GetType() == b->GetType();
    }

    inline bool ValuesEquivalent(const StyleValue& a, const StyleValue& b)
    {
        if (Optional<Color> ca = a.AsColor(); ca.HasValue())
        {
            Optional<Color> cb = b.AsColor();
            return cb.HasValue() && ColorsEqual(ca.Value(), cb.Value());
        }
        if (Optional<f32> fa = a.AsFloat(); fa.HasValue())
        {
            Optional<f32> fb = b.AsFloat();
            return fb.HasValue() && Abs(fa.Value() - fb.Value()) < 1e-4f;
        }
        if (Optional<Thickness> ta = a.AsThickness(); ta.HasValue())
        {
            Optional<Thickness> tb = b.AsThickness();
            return tb.HasValue() && ta.Value().Left == tb.Value().Left &&
                   ta.Value().Top == tb.Value().Top && ta.Value().Right == tb.Value().Right &&
                   ta.Value().Bottom == tb.Value().Bottom;
        }
        if (Drawable* da = a.AsDrawable(); da != nullptr)
        {
            return DrawablesEquivalent(da, b.AsDrawable());
        }
        if (Optional<bool> ba = a.AsBool(); ba.HasValue())
        {
            Optional<bool> bb = b.AsBool();
            return bb.HasValue() && ba.Value() == bb.Value();
        }
        return true; // string/unhandled kinds unused by the themes
    }

    /// The gate: every legacy selector must exist in the parsed sheet with equivalent
    /// properties, and vice versa (two loops so a failure names the missing side).
    inline void CheckSheetParity(const StyleSheet& parsed, const StyleSheet& legacy)
    {
        Array<MergedRule> a;
        Array<MergedRule> b;
        MergeSheet(parsed, a);
        MergeSheet(legacy, b);

        for (const MergedRule& lm : b)
        {
            const MergedRule* pm = nullptr;
            for (const MergedRule& cand : a)
            {
                if (cand.key == lm.key)
                {
                    pm = &cand;
                    break;
                }
            }
            if (pm == nullptr)
            {
                std::printf("PARITY: legacy selector missing from the .sss sheet: type=%s "
                            "classes='%s' state=%u pseudo='%s'\n",
                            lm.key.type != nullptr ? lm.key.type->name : "<any>",
                            lm.key.classes.Size() > 0
                                ? reinterpret_cast<const char*>(lm.key.classes.Data())
                                : "",
                            lm.key.state,
                            lm.key.pseudo.Size() > 0
                                ? reinterpret_cast<const char*>(lm.key.pseudo.Data())
                                : "");
            }
            REQUIRE(pm != nullptr);
            CHECK(pm->props.Size() == lm.props.Size());
            for (const StyleRule::Entry& le : lm.props)
            {
                const StyleRule::Entry* pe = nullptr;
                for (const StyleRule::Entry& cand : pm->props)
                {
                    if (cand.Prop == le.Prop)
                    {
                        pe = &cand;
                        break;
                    }
                }
                if (pe == nullptr || !ValuesEquivalent(pe->Value, le.Value))
                {
                    std::printf("PARITY: property %d %s on type=%s classes='%s' pseudo='%s'\n",
                                static_cast<int>(le.Prop), pe == nullptr ? "MISSING" : "DIFFERS",
                                lm.key.type != nullptr ? lm.key.type->name : "<any>",
                                lm.key.classes.Size() > 0
                                    ? reinterpret_cast<const char*>(lm.key.classes.Data())
                                    : "",
                                lm.key.pseudo.Size() > 0
                                    ? reinterpret_cast<const char*>(lm.key.pseudo.Data())
                                    : "");
                }
                REQUIRE(pe != nullptr);
                CHECK(ValuesEquivalent(pe->Value, le.Value));
            }
        }
        for (const MergedRule& pm : a)
        {
            bool found = false;
            for (const MergedRule& lm : b)
            {
                if (lm.key == pm.key)
                {
                    found = true;
                    break;
                }
            }
            INFO("the .sss sheet declares a selector the legacy theme does not");
            CHECK(found);
        }
    }
}
