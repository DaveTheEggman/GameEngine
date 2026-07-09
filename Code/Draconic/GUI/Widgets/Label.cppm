// Draconic GUI - :label partition
//
// Label: a UIWidget that displays a line of text. The thinnest real control - it composes
// the Phase-4 Text primitive (drawn in the padding-inset content bounds) onto the widget
// base. Ported from eepp's UITextView. Wrapping / rich text follow the Text primitive's
// own growth.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:label;

import draconic.core;    // StringView, Color, Float2
import draconic.fonts;   // CachedFont
import :rect;
import :draw_context;
import :text;
import :ui_widget;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

export namespace draconic::gui
{
    class Label : public UIWidget
    {
        DRACONIC_OBJECT(Label, UIWidget)
    public:
        Label() { m_text.SetAlignment(TextHAlign::Left, TextVAlign::Middle); }

        void SetText(core::StringView text) { m_text.SetString(text); Invalidate(); }
        [[nodiscard]] core::StringView GetText() const { return m_text.GetString(); }

        void SetFont(fonts::CachedFont* font) { m_text.SetFont(font); Invalidate(); }
        [[nodiscard]] fonts::CachedFont* GetFont() const { return m_text.GetFont(); }

        void SetTextColor(Color color) { m_text.SetColor(color); Invalidate(); }
        [[nodiscard]] Color GetTextColor() const { return m_text.GetColor(); }

        void SetTextAlignment(TextHAlign horizontal, TextVAlign vertical)
        {
            m_text.SetAlignment(horizontal, vertical);
            Invalidate();
        }

        // The natural size of the text (for layout).
        [[nodiscard]] core::Float2 MeasureText() const { return m_text.Measure(); }

    protected:
        void OnDraw(DrawContext& ctx, const Rect& localBounds) override
        {
            (void)localBounds;
            m_text.Draw(ctx, GetContentBounds()); // aligned within the padding-inset bounds
        }

        Text m_text;
    };

    DRACONIC_DEFINE_OBJECT(Label, "draconic::gui")
}
