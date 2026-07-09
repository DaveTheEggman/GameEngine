// Draconic GUI - :text_field partition
//
// TextField: a single-line, editable text control - the first widget driven by the keyboard
// and text-input path (everything before it was mouse-only). A lean Draconic-native control
// modeled on eepp's UITextInput (role only): it maintains an editable UTF-8 buffer and a
// caret (a byte offset at a codepoint boundary), inserts characters from OnTextInput, and
// handles the editing/navigation keys (backspace/delete/left/right/home/end) via OnKeyDown.
// A mouse press places the caret and focuses the field. The caret blinks only when focused.
//
// Selection, clipboard, multi-line, and horizontal scroll of an over-long value are follow-
// ups; v1 is a usable single-line editor that closes the "text never proven end-to-end" gap.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:text_field;

import draconic.core;    // String, StringView, Color, Function, Move, Min, Max, Utf8*Boundary
import draconic.fonts;   // CachedFont
import draconic.vg;      // CornerRadii
import :rect;
import :draw_context;
import :text;
import :event;
import :ui_widget;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;
namespace vg = draconic::vg;

export namespace draconic::gui
{
    class TextField : public UIWidget
    {
        DRACONIC_OBJECT(TextField, UIWidget)
    public:
        TextField()
        {
            SetTag(core::StringView(u8"textfield"));
            m_text.SetAlignment(TextHAlign::Left, TextVAlign::Middle);
            SetTabFocusable(true);
        }

        // === Value ===
        void SetText(core::StringView text)
        {
            m_value = core::String(text);
            m_caret = m_value.Size();     // caret to end
            SyncText();
            NotifyChanged();
        }
        [[nodiscard]] core::StringView GetText() const { return m_value.AsView(); }

        // Byte offset of the caret (always on a codepoint boundary).
        [[nodiscard]] usize GetCaret() const noexcept { return m_caret; }
        void SetCaret(usize byteOffset)
        {
            m_caret = core::Min(byteOffset, m_value.Size());
            Invalidate();
        }

        void SetOnTextChanged(core::Function<void(core::StringView)> callback) { m_onChanged = core::Move(callback); }

        // An enabled TextField wants platform text input while focused (the bridge enables
        // the window's IME accordingly).
        [[nodiscard]] bool WantsTextInput() const override { return IsEnabled(); }

        // === Appearance ===
        void SetFont(fonts::CachedFont* font) { m_text.SetFont(font); Invalidate(); }
        [[nodiscard]] fonts::CachedFont* GetFont() const { return m_text.GetFont(); }
        void SetTextColor(Color color) { m_text.SetColor(color); Invalidate(); }
        void SetCaretColor(Color color) { m_caretColor = color; Invalidate(); }

        // The caret blink is advanced by the owner (SceneNode::Update path) if wired; a static
        // caret (always shown while focused) is the default when not ticked.
        void Update(f64 deltaSeconds)
        {
            if (!IsFocused()) return;
            m_blinkAccum += deltaSeconds;
            if (m_blinkAccum >= kBlinkPeriod)
            {
                m_blinkAccum -= kBlinkPeriod;
                m_caretVisible = !m_caretVisible;
                Invalidate();
            }
        }

    protected:
        void OnTextInput(const TextInputEvent& event) override
        {
            if (!IsEnabled() || event.Text.Size() == 0) return;
            m_value.Insert(m_caret, event.Text);
            m_caret += event.Text.Size();
            SyncText();
            ResetBlink();
            NotifyChanged();
        }

        void OnKeyDown(const KeyEvent& event) override
        {
            if (!IsEnabled()) return;
            switch (static_cast<KeyCode>(event.KeyCode))
            {
            case KeyCode::Backspace:
                if (m_caret > 0)
                {
                    const usize prev = core::Utf8PrevBoundary(m_value.AsView(), m_caret);
                    m_value.Remove(prev, m_caret - prev);
                    m_caret = prev;
                    SyncText();
                    NotifyChanged();
                }
                break;
            case KeyCode::Delete:
                if (m_caret < m_value.Size())
                {
                    const usize next = core::Utf8NextBoundary(m_value.AsView(), m_caret);
                    m_value.Remove(m_caret, next - m_caret);
                    SyncText();
                    NotifyChanged();
                }
                break;
            case KeyCode::Left:
                m_caret = core::Utf8PrevBoundary(m_value.AsView(), m_caret);
                break;
            case KeyCode::Right:
                m_caret = core::Utf8NextBoundary(m_value.AsView(), m_caret);
                break;
            case KeyCode::Home:
                m_caret = 0;
                break;
            case KeyCode::End:
                m_caret = m_value.Size();
                break;
            default:
                return; // don't reset the blink for keys we ignore
            }
            ResetBlink();
            Invalidate();
        }

        void OnMouseDown(const MouseEvent& event) override
        {
            UINode::OnMouseDown(event);
            RequestFocus();
            m_caret = CaretFromX(ConvertToNodeSpace(event.Position).x);
            ResetBlink();
        }

        void OnFocusGained() override { ResetBlink(); Invalidate(); }
        void OnFocusLost() override { m_caretVisible = false; Invalidate(); }

        void OnDraw(DrawContext& ctx, const Rect& localBounds) override
        {
            (void)localBounds;
            const Rect content = GetContentBounds();
            m_text.Draw(ctx, content);

            if (IsFocused() && m_caretVisible)
            {
                const f32 caretX = content.x + PrefixWidth(m_caret);
                const f32 h = m_text.GetLineHeight();
                const f32 cy = content.y + content.height * 0.5f;
                const f32 top = cy - h * 0.5f;
                ctx.VG().FillRect(core::Rectangle{ caretX, top, kCaretWidth, h }, m_caretColor);
            }
        }

    private:
        void SyncText() { m_text.SetString(m_value.AsView()); Invalidate(); }
        void NotifyChanged() { if (m_onChanged) m_onChanged(m_value.AsView()); }
        void ResetBlink() { m_caretVisible = true; m_blinkAccum = 0.0; Invalidate(); }

        // Width of the value's prefix [0, byteOffset) in the current font (0 if no font).
        [[nodiscard]] f32 PrefixWidth(usize byteOffset) const
        {
            fonts::CachedFont* font = m_text.GetFont();
            if (font == nullptr || font->font == nullptr || byteOffset == 0) return 0.0f;
            return font->font->MeasureString(m_value.AsView().SubStr(0, byteOffset));
        }

        // The caret byte offset whose prefix width is closest to local x (within content).
        [[nodiscard]] usize CaretFromX(f32 localX) const
        {
            const Rect content = GetContentBounds();
            const f32 target = localX - content.x;
            if (target <= 0.0f) return 0;

            usize best = 0;
            f32 bestDist = target; // distance at offset 0 is |target - 0|
            usize offset = 0;
            const core::StringView view = m_value.AsView();
            while (offset < view.Size())
            {
                offset = core::Utf8NextBoundary(view, offset);
                const f32 w = PrefixWidth(offset);
                const f32 dist = (w >= target) ? (w - target) : (target - w);
                if (dist < bestDist) { bestDist = dist; best = offset; }
            }
            return best;
        }

        core::String m_value;
        usize m_caret = 0;                 // byte offset, codepoint boundary
        Text m_text;
        Color m_caretColor{ 0.90f, 0.92f, 0.95f, 1.0f };
        core::Function<void(core::StringView)> m_onChanged;

        // Blink state (only advances while focused, via Update()).
        bool m_caretVisible = true;
        f64 m_blinkAccum = 0.0;

        static constexpr f64 kBlinkPeriod = 0.5;  // seconds per on/off half-cycle
        static constexpr f32 kCaretWidth = 1.5f;
    };

    DRACONIC_DEFINE_OBJECT(TextField, "draconic::gui")
}
