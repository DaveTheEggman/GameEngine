// Draconic UI - :edit_text partition
//
// Single-line and multiline text input control. Implements ITextEditHost for TextEditingBehavior;
// supports selection, cursor, clipboard, undo/redo, input filtering, prefix/suffix decorations.
// Ported from Sedulous.UI/src/Controls/EditText.bf.
//
// TEXT RENDERING / GLYPH SHAPING DEFERRED (the same deferral as every other control - no Fonts
// service is wired into UIContext yet, and DrawRootView is deferred). Concretely:
//   - Glyph shaping / the glyph-position cache is omitted; the hit-test / cursor-X / cursor-Y
//     ITextEditHost methods return 0 (fallbacks), LineHeight() falls back to the font size.
//   - OnMeasure uses the font-size height (Sedulous's FontService-null branch).
//   - OnDraw paints only the background + focus border; text/selection/cursor drawing is deferred.
//   - Multiline Up/Down navigation, scroll, and the right-click ContextMenu (control not ported) are
//     therefore inert. All CHARACTER-index editing (cursor L/R/Home/End, select-all, insert/delete,
//     undo/redo, MaxLength, InputFilter) is fully live - it goes through TextEditingBehavior and needs
//     no font metrics. Everything un-defers when fonts::IFontService is wired into UIContext.
//
// The host accessors GetMaxLength()/GetIsReadOnly() are the `Get`-prefixed ITextEditHost members
// (EditText's identically-named Property<> fields would otherwise collide - see :itext_edit_host).
// Beef `[Friend]mBehavior` test access -> a public Behavior() accessor; Beef property setters -> Set*.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:edit_text;

import draconic.core;   // String, StringView, RefPtr, Utf8Length, DecodeCodepoint
import draconic.vg;
import :view;
import :property;
import :event;
import :box_constraints;
import :style_property;
import :draw_context;
import :drawable;
import :control_state;
import :event_args;
import :input_enums;
import :enums;
import :iclipboard;
import :itext_edit_host;
import :text_editing_behavior;
import :input_filter;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::ui
{
    class EditText : public View, public ITextEditHost
    {
        DRACONIC_OBJECT(EditText, View)
    public:
        // === Text state (faithful public Property<> fields; tests drive these) ===
        Property<String> Placeholder;
        Property<bool> IsReadOnly{ false };
        Property<bool> Multiline{ false };
        Property<i32> MaxLength{ 0 };

        /// Whether right-click shows the Cut/Copy/Paste context menu (menu itself deferred).
        bool ShowContextMenuOnRightClick = true;

        // === Events ===
        Event<void(EditText*)> OnTextChanged;
        Event<void(EditText*)> OnSubmit;

        EditText() : m_behavior(this)
        {
            IsFocusable = true;
            IsTabStop = true;
            WantsArrowKeys = true;
            Cursor = CursorType::IBeam;

            Placeholder.SetOwner(this, InvalidationKind::Visual);
            IsReadOnly.SetOwner(this, InvalidationKind::Visual);
            Multiline.SetOwner(this);
            MaxLength.SetOwner(this);
        }

        // === Public text API ===
        void SetText(StringView text)
        {
            m_text = String(text);
            m_behavior.Reset();
            Invalidate();
        }

        void SetPlaceholder(StringView text) { Placeholder.SetValue(String(text)); Invalidate(); }

        // === Behavior passthrough (Beef properties -> methods; [Friend] access -> Behavior()) ===
        [[nodiscard]] TextEditingBehavior& Behavior() noexcept { return m_behavior; }
        [[nodiscard]] InputFilter* Filter() { return m_behavior.Filter(); }
        void SetFilter(InputFilter filter) { m_behavior.SetFilter(Move(filter)); }

        [[nodiscard]] i32 CursorPosition() const noexcept { return m_behavior.CursorPosition(); }
        [[nodiscard]] i32 SelectionStart() const noexcept { return m_behavior.SelectionStart(); }
        [[nodiscard]] i32 SelectionEnd() const noexcept { return m_behavior.SelectionEnd(); }

        // === Prefix / suffix (storage + API faithful; measure/draw deferred with text render) ===
        void SetPrefix(StringView text) { m_prefixView = nullptr; m_prefixText = String(text); m_hasPrefixText = true; Invalidate(); }
        void SetPrefix(View* view) { m_hasPrefixText = false; m_prefixText.Clear(); m_prefixView = RefPtr<View>(view); Invalidate(); }
        void SetSuffix(StringView text) { m_suffixView = nullptr; m_suffixText = String(text); m_hasSuffixText = true; Invalidate(); }
        void SetSuffix(View* view) { m_hasSuffixText = false; m_suffixText.Clear(); m_suffixView = RefPtr<View>(view); Invalidate(); }

        // === ITextEditHost ===
        [[nodiscard]] StringView Text() const override { return m_text; }
        [[nodiscard]] i32 GetMaxLength() const override { return MaxLength.Value(); }
        [[nodiscard]] bool GetIsReadOnly() const override { return IsReadOnly.Value(); }
        [[nodiscard]] bool IsMultiline() const override { return Multiline.Value(); }
        [[nodiscard]] i32 TextCharCount() const override { return static_cast<i32>(Utf8Length(m_text)); }

        void ReplaceText(i32 charStart, i32 charLength, StringView replacement) override
        {
            const i32 byteStart = CharToByteOffset(m_text, charStart);
            const i32 byteEnd = CharToByteOffset(m_text, charStart + charLength);
            const i32 byteLength = byteEnd - byteStart;
            m_text.Remove(static_cast<usize>(byteStart), static_cast<usize>(byteLength));
            m_text.Insert(static_cast<usize>(byteStart), replacement);
            // (glyph cache invalidation deferred with text rendering)
        }

        void OnTextModified() override
        {
            // (glyph-dirty / cursor-blink / cursor-scroll deferred with text rendering)
            Invalidate();
            OnTextChanged.Invoke(this);
        }

        // Hit-test / cursor geometry need the glyph shaper (deferred) -> fallbacks.
        [[nodiscard]] i32 HitTestPosition(f32, f32) override { return 0; }
        [[nodiscard]] i32 HitTestGlyphPosition(f32, f32) override { return 0; }
        [[nodiscard]] f32 GetCursorXPosition(i32) override { return 0.0f; }
        [[nodiscard]] f32 GetCursorYPosition(i32) override { return 0.0f; }
        [[nodiscard]] f32 LineHeight() override { return ResolveStyleFloat(StyleProperty::FontSize, 14.0f); }
        [[nodiscard]] IClipboard* Clipboard() override { return Context ? Context->Clipboard() : nullptr; }
        [[nodiscard]] f32 CurrentTime() override { return Context ? Context->TotalTime() : 0.0f; }

        // === Input handlers ===
        void OnMouseDown(MouseEventArgs& e) override
        {
            if (!IsEffectivelyEnabled()) { return; }

            if (e.Button == MouseButton::Right && ShowContextMenuOnRightClick)
            {
                // (right-click Cut/Copy/Paste ContextMenu deferred - ContextMenu control not ported)
                e.Handled = true;
                return;
            }
            if (e.Button != MouseButton::Left) { return; }

            if (e.ClickCount <= 1)
            {
                m_isDragging = true;
                if (Context != nullptr) { Context->GetFocusManager()->SetCapture(this); }
            }
            m_behavior.HandleMouseDown(e.X, e.Y, e.ClickCount, e.Modifiers);
            e.Handled = true;
        }

        void OnMouseMove(MouseEventArgs& e) override
        {
            if (m_isDragging) { m_behavior.HandleMouseMove(e.X, e.Y); }
        }

        void OnMouseUp(MouseEventArgs& e) override
        {
            if (e.Button != MouseButton::Left) { return; }
            if (m_isDragging)
            {
                m_isDragging = false;
                if (Context != nullptr) { Context->GetFocusManager()->ReleaseCapture(); }
                e.Handled = true;
            }
        }

        void OnKeyDown(KeyEventArgs& e) override
        {
            if (!IsEffectivelyEnabled()) { return; }

            // Enter -> submit for single-line; newline for multiline (handled by the behavior).
            if (e.Key == KeyCode::Return && !Multiline.Value())
            {
                OnSubmit.Invoke(this);
                e.Handled = true;
                return;
            }
            m_behavior.HandleKeyDown(e.Key, e.Modifiers);
            e.Handled = true;
        }

        void OnTextInput(TextInputEventArgs& e) override
        {
            if (!IsEffectivelyEnabled()) { return; }
            m_behavior.HandleTextInput(e.Character);
            e.Handled = true;
        }

        void OnFocusLost() override { m_isDragging = false; }

        void OnActivate() override { OnSubmit.Invoke(this); }

        // Text to display; overridden by PasswordBox for masking. Public so tests can inspect it
        // (Beef exercised it via [Friend]); it is the PasswordBox masking extension point.
        virtual void GetDisplayText(String& outText) const { outText = m_text; }

    protected:
        void OnMeasure(BoxConstraints constraints) override
        {
            const f32 fontSize = ResolveStyleFloat(StyleProperty::FontSize, 14.0f);
            const Thickness padding = ResolveStyleThickness(StyleProperty::Padding, Thickness{ 6, 4 });
            // Text measuring deferred: font-size height (Sedulous's FontService-null branch).
            const f32 textH = fontSize;
            const f32 minWidth = 100.0f + padding.TotalHorizontal();
            const f32 totalH = textH + padding.TotalVertical();
            MeasuredSize = Float2{ constraints.ConstrainWidth(minWidth), constraints.ConstrainHeight(totalH) };
        }

        void OnDraw(UIDrawContext& ctx) override
        {
            const Rectangle bounds{ 0, 0, Width(), Height() };

            // Background drawable from theme (fallback fill otherwise).
            if (Drawable* bg = ResolveStyleDrawable(StyleProperty::Background))
            {
                bg->Draw(ctx, bounds, GetControlState());
            }
            else
            {
                ctx.VG().FillRect(bounds, Color{ 30.0f / 255.0f, 32.0f / 255.0f, 42.0f / 255.0f, 1.0f });
            }

            // Focused border overlay.
            if (IsFocused())
            {
                const Color accent = ResolveStyleColor(StyleProperty::AccentColor,
                    ResolveStyleColor(StyleProperty::CursorColor, Color{ 80.0f / 255.0f, 160.0f / 255.0f, 1.0f, 1.0f }));
                const f32 cr = ResolveStyleFloat(StyleProperty::CornerRadius);
                if (cr > 0) { ctx.VG().StrokeRoundedRect(bounds, cr, accent, 2.0f); }
                else { ctx.VG().StrokeRect(bounds, accent, 2.0f); }
            }
            // (prefix/suffix + text/selection/cursor drawing deferred with text rendering)
        }

        // Convert a character index to a byte offset in a UTF-8 string.
        [[nodiscard]] static i32 CharToByteOffset(StringView text, i32 charIndex)
        {
            i32 charCount = 0;
            usize byteOffset = 0;
            usize i = 0;
            while (i < text.Size())
            {
                if (charCount >= charIndex) { break; }
                (void)DecodeCodepoint(text, i);
                charCount++;
                byteOffset = i;
            }
            if (charCount < charIndex) { return static_cast<i32>(text.Size()); }
            return static_cast<i32>(byteOffset);
        }

        String m_text;
        TextEditingBehavior m_behavior;
        bool m_isDragging = false;

        // Prefix / suffix decorations (drawing deferred).
        String m_prefixText;
        String m_suffixText;
        bool m_hasPrefixText = false;
        bool m_hasSuffixText = false;
        RefPtr<View> m_prefixView;
        RefPtr<View> m_suffixView;
    };

    DRACONIC_DEFINE_OBJECT(EditText, "draconic::ui")
}
