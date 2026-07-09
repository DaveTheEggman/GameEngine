// Draconic GUI - :text partition
//
// Text: a measurable, drawable run of text on a font, rendered through the DrawContext/VG
// seam. Derived from eepp's Graphics::Text (the object UITextView/labels cache), adapted to
// Draconic: it holds a non-owning draconic.fonts CachedFont (owned by the font service),
// measures via IFont metrics, and draws via VG's DrawText. Rich styling (per-range colors,
// outline/shadow, wrap, bidi) is deferred to later text work.

module;
#include "Core/Prelude.h"

export module draconic.gui:text;

import draconic.core;    // String, StringView, Float2, Color
import draconic.fonts;   // CachedFont, IFont, FontMetrics
import :rect;
import :draw_context;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

export namespace draconic::gui
{
    enum class TextHAlign { Left, Center, Right };
    enum class TextVAlign { Top, Middle, Bottom };

    class Text
    {
    public:
        Text() = default;
        Text(core::StringView string, fonts::CachedFont* font, Color color = Color::White)
            : m_string(string), m_font(font), m_color(color) {}

        void SetString(core::StringView string) { m_string = string; }
        [[nodiscard]] core::StringView GetString() const { return m_string.AsView(); }
        [[nodiscard]] bool IsEmpty() const { return m_string.AsView().Size() == 0; }

        void SetFont(fonts::CachedFont* font) { m_font = font; }
        [[nodiscard]] fonts::CachedFont* GetFont() const noexcept { return m_font; }

        void SetColor(Color color) noexcept { m_color = color; }
        [[nodiscard]] Color GetColor() const noexcept { return m_color; }

        void SetAlignment(TextHAlign horizontal, TextVAlign vertical) noexcept { m_hAlign = horizontal; m_vAlign = vertical; }
        [[nodiscard]] TextHAlign GetHAlign() const noexcept { return m_hAlign; }
        [[nodiscard]] TextVAlign GetVAlign() const noexcept { return m_vAlign; }

        // === Measurement (0 if no font) ===
        [[nodiscard]] f32 GetWidth() const
        {
            return HasFont() ? m_font->font->MeasureString(m_string.AsView()) : 0.0f;
        }
        [[nodiscard]] f32 GetLineHeight() const
        {
            return HasFont() ? m_font->font->Metrics().lineHeight : 0.0f;
        }
        [[nodiscard]] core::Float2 Measure() const { return core::Float2{ GetWidth(), GetLineHeight() }; }

        // The top-left draw position for the current alignment within `bounds`.
        [[nodiscard]] core::Float2 AlignedPosition(const Rect& bounds) const
        {
            const f32 w = GetWidth();
            const f32 h = GetLineHeight();
            f32 x = bounds.Left();
            f32 y = bounds.Top();
            if (m_hAlign == TextHAlign::Center)      x += (bounds.width - w) * 0.5f;
            else if (m_hAlign == TextHAlign::Right)  x += (bounds.width - w);
            if (m_vAlign == TextVAlign::Middle)      y += (bounds.height - h) * 0.5f;
            else if (m_vAlign == TextVAlign::Bottom) y += (bounds.height - h);
            return core::Float2{ x, y };
        }

        // === Drawing ===
        void Draw(DrawContext& ctx, core::Float2 position) const
        {
            if (m_font == nullptr || IsEmpty()) return;
            ctx.VG().DrawText(m_string.AsView(), m_font, position, m_color);
        }
        void Draw(DrawContext& ctx, const Rect& bounds) const
        {
            if (m_font == nullptr || IsEmpty()) return;
            ctx.VG().DrawText(m_string.AsView(), m_font, AlignedPosition(bounds), m_color);
        }

    private:
        [[nodiscard]] bool HasFont() const noexcept { return m_font != nullptr && m_font->font != nullptr; }

        core::String m_string;
        fonts::CachedFont* m_font = nullptr; // non-owning (owned by the font service)
        Color m_color = Color::White;
        TextHAlign m_hAlign = TextHAlign::Left;
        TextVAlign m_vAlign = TextVAlign::Top;
    };
}
