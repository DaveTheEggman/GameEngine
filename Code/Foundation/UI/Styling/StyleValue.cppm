// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI - :style_value partition
//
// A tagged value stored in a StyleRule: a discriminated union of Color / Float / Thickness /
// Drawable / Bool / String / None. Ported from Sedulous.UI/src/Styling/StyleValue.bf.
//
// Divergence (language): Beef discriminated union -> a kind + separate storage members. This makes
// the ownership Beef managed by hand (DrawableRef AddRef/Release, StringRef copy/delete) automatic:
// the Drawable is a RefPtr<Drawable> and the string is an owned String, so copy/move/destroy of a
// StyleValue is correct with no manual refcounting.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui:style_value;

import foundation.core; // Color, Optional, RefPtr, String, StringView, HashText
import :thickness;
import :unit;
import :drawable;

using namespace foundation::core;
namespace core = foundation::core;

export namespace foundation::ui
{
    class StyleValue;

    /// A `var(--name, fallback)` reference: resolved against the view's custom properties at
    /// COMPUTE time (never at parse time - the value depends on where the view sits). Shared
    /// by every copy of the StyleValue that carries it; immutable once built.
    class VariableReference : public Object
    {
        RTTI_OBJECT(VariableReference, Object)
    public:
        String Name;     ///< Including the leading `--`.
        u64 NameHash = 0; ///< HashText(Name)
        /// Used when the variable is unset (may itself be a reference: nested fallback).
        /// Owned here through a pointer to keep StyleValue's own definition non-recursive.
        RefPtr<Object> Fallback; // a StyleValueBox (defined below)

        VariableReference() = default;
    };

    class StyleValue
    {
    public:
        enum class Kind
        {
            None,
            Color,
            Float,
            Thickness,
            Drawable,
            Bool,
            String,
            /// A length with units (percent/em/calc sums); plain numbers stay Float.
            Length,
            /// The `inherit` keyword: take the parent's computed value.
            Inherit,
            /// The `initial` keyword: as if unset (the property's default).
            Initial,
            /// A `var(--name, fallback)` reference (see VariableReference).
            Variable
        };

        StyleValue() = default; // None

        [[nodiscard]] static StyleValue ColorVal(core::Color c)
        {
            StyleValue v;
            v.m_kind = Kind::Color;
            v.m_color = c;
            return v;
        }
        [[nodiscard]] static StyleValue FloatVal(f32 f)
        {
            StyleValue v;
            v.m_kind = Kind::Float;
            v.m_float = f;
            return v;
        }
        [[nodiscard]] static StyleValue ThicknessVal(Thickness t)
        {
            StyleValue v;
            v.m_kind = Kind::Thickness;
            v.m_thickness = t;
            return v;
        }
        [[nodiscard]] static StyleValue DrawableRef(RefPtr<Drawable> d)
        {
            StyleValue v;
            v.m_kind = Kind::Drawable;
            v.m_drawable = Move(d);
            return v;
        }
        [[nodiscard]] static StyleValue BoolVal(bool b)
        {
            StyleValue v;
            v.m_kind = Kind::Bool;
            v.m_bool = b;
            return v;
        }
        [[nodiscard]] static StyleValue StringRef(StringView s)
        {
            StyleValue v;
            v.m_kind = Kind::String;
            v.m_string = String(s);
            return v;
        }
        [[nodiscard]] static StyleValue LengthVal(Unit length)
        {
            StyleValue v;
            v.m_kind = Kind::Length;
            v.m_length = length;
            return v;
        }
        [[nodiscard]] static StyleValue Inherit()
        {
            StyleValue v;
            v.m_kind = Kind::Inherit;
            return v;
        }
        [[nodiscard]] static StyleValue Initial()
        {
            StyleValue v;
            v.m_kind = Kind::Initial;
            return v;
        }
        /// A reference to a custom property. `name` includes the leading `--`; the overload
        /// with `fallback` is used when the variable is unset.
        [[nodiscard]] static StyleValue VariableRef(IAllocator& allocator, StringView name);
        [[nodiscard]] static StyleValue VariableRef(IAllocator& allocator, StringView name,
                                                    const StyleValue& fallback);
        [[nodiscard]] static StyleValue None() { return StyleValue{}; }

        [[nodiscard]] Kind GetKind() const noexcept { return m_kind; }
        [[nodiscard]] bool IsNone() const noexcept { return m_kind == Kind::None; }
        /// Inherit/Initial/Variable: needs the cascade to turn it into a concrete value.
        [[nodiscard]] bool NeedsResolution() const noexcept
        {
            return m_kind == Kind::Inherit || m_kind == Kind::Initial || m_kind == Kind::Variable;
        }

        [[nodiscard]] Optional<Unit> AsLength() const
        {
            if (m_kind == Kind::Length)
            {
                return m_length;
            }
            return {};
        }
        /// The reference for a Variable value (null otherwise).
        [[nodiscard]] const VariableReference* Variable() const noexcept
        {
            return m_kind == Kind::Variable ? m_variable.Get() : nullptr;
        }

        /// Try to get as Color / Float / Thickness / Bool (empty Optional if the kind differs).
        [[nodiscard]] Optional<core::Color> AsColor() const
        {
            if (m_kind == Kind::Color)
            {
                return m_color;
            }
            return {};
        }
        [[nodiscard]] Optional<f32> AsFloat() const
        {
            if (m_kind == Kind::Float)
            {
                return m_float;
            }
            return {};
        }
        [[nodiscard]] Optional<Thickness> AsThickness() const
        {
            if (m_kind == Kind::Thickness)
            {
                return m_thickness;
            }
            return {};
        }
        [[nodiscard]] Optional<bool> AsBool() const
        {
            if (m_kind == Kind::Bool)
            {
                return m_bool;
            }
            return {};
        }

        /// Borrowed drawable pointer (owned by this value), or null if the kind differs.
        [[nodiscard]] Drawable* AsDrawable() const
        {
            return m_kind == Kind::Drawable ? m_drawable.Get() : nullptr;
        }
        /// Borrowed string view (backing owned by this value), or empty if the kind differs.
        [[nodiscard]] Optional<StringView> AsString() const
        {
            if (m_kind == Kind::String)
            {
                return m_string.AsView();
            }
            return {};
        }

    private:
        Kind m_kind = Kind::None;
        core::Color m_color{};
        f32 m_float = 0.0f;
        Thickness m_thickness{};
        RefPtr<Drawable> m_drawable;
        bool m_bool = false;
        String m_string;
        Unit m_length{};
        RefPtr<VariableReference> m_variable;
    };

    /// Boxes a StyleValue as an Object so a VariableReference can own its fallback.
    class StyleValueBox : public Object
    {
        RTTI_OBJECT(StyleValueBox, Object)
    public:
        StyleValue Value;
        StyleValueBox() = default;
        explicit StyleValueBox(StyleValue value) : Value(Move(value)) {}
    };

    inline StyleValue StyleValue::VariableRef(IAllocator& allocator, StringView name)
    {
        return VariableRef(allocator, name, StyleValue{});
    }

    inline StyleValue StyleValue::VariableRef(IAllocator& allocator, StringView name,
                                              const StyleValue& fallback)
    {
        RefPtr<VariableReference> ref = MakeRef<VariableReference>(allocator);
        ref->Name = String(name);
        ref->NameHash = HashText(name);
        if (!fallback.IsNone())
        {
            ref->Fallback = MakeRef<StyleValueBox>(allocator, fallback);
        }
        StyleValue v;
        v.m_kind = Kind::Variable;
        v.m_variable = Move(ref);
        return v;
    }

    /// The fallback of a reference as a StyleValue (None when there is none).
    [[nodiscard]] inline StyleValue VariableFallback(const VariableReference& ref)
    {
        if (const StyleValueBox* box = Cast<StyleValueBox>(ref.Fallback.Get()))
        {
            return box->Value;
        }
        return StyleValue::None();
    }

    RTTI_DEFINE_OBJECT(VariableReference, "rtti::ui")
    RTTI_DEFINE_OBJECT(StyleValueBox, "rtti::ui")
}
