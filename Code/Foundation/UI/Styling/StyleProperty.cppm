// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI - :style_property partition
//
// Identifies a style property settable in a StyleRule. COUNT sizes property arrays.
// Ported from Sedulous.UI/src/Styling/StyleProperty.bf.

export module foundation.ui:style_property;

export namespace foundation::ui
{
    enum class StyleProperty
    {
        // Drawable properties
        Background,
        CheckedBackground,
        MenuItemHoverDrawable,

        // Color properties
        TextColor,
        TextDimColor,
        PlaceholderColor,
        BorderColor,
        CursorColor,
        SelectionColor,
        AccentColor,
        // Semantic status colors (palette $success/$warning/$error; toasts, validation).
        SuccessColor,
        WarningColor,
        ErrorColor,

        // Float properties
        FontSize,
        FontFamily,
        CornerRadius,
        BorderWidth,
        Spacing,
        Opacity,
        Width,
        Height,

        // Thickness properties
        Padding,
        Margin,

        // Bool properties
        WordWrap,

        // === P2 box model (appended: the parser's range predicates depend on the order above) ===
        /// `box-shadow: x y blur spread color [inset]` (a Shadow value).
        BoxShadow,
        // Length properties feeding LayoutStyle (see View::RefreshEffectiveLayout).
        MinWidth,
        MinHeight,
        MaxWidth,
        MaxHeight,
        Top,
        Right,
        Bottom,
        Left,
        // Float properties feeding LayoutStyle.
        ZIndex,
        FlexGrow,
        FlexShrink,
        // Keyword (String) properties.
        /// `static` | `absolute`.
        Position,
        /// `visible` | `hidden` (hidden clips children to the border box).
        Overflow,
        /// `start` | `end` | `center` | `stretch` | `baseline`.
        AlignSelf,

        /// Number of known properties (for array sizing).
        COUNT
    };
}
