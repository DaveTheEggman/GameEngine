// Draconic UI - :editable_label partition
//
// Displays as a plain text label and switches to an editable field on BeginEdit() (double-click or
// slow-click). Extends EditText for cursor/selection/clipboard in edit mode. Ported from
// Sedulous.UI/src/Controls/EditableLabel.bf.
//
// In label mode: read-only, not focusable, draws plain (optionally ellipsized) text. In edit mode:
// editable, focusable, draws EditText's text content + accent border. Port taxes: Beef `new SetText`
// (hiding) -> a same-named method calling EditText::SetText; `newText.IsWhiteSpace`/Length==0 ->
// core::Trimmed(newText).IsEmpty(); ValidateRename delegate -> Function<bool(StringView)>; Text.RawChars
// ellipsis loop -> byte-tracked codepoint decode. EditText::DrawTextContent is protected for reuse here.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui:editable_label;

import draconic.core;
import draconic.vg;
import draconic.fonts;
import :edit_text;
import :view;
import :property;
import :event;
import :style_property;
import :draw_context;
import :drawable;
import :control_state;
import :event_args;
import :input_enums;
import :enums;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

export namespace draconic::ui
{
    class EditableLabel : public EditText
    {
        DRACONIC_OBJECT(EditableLabel, EditText)
    public:
        Property<f32> TextOffsetX{ 0.0f };
        Property<fonts::TextAlignment> HAlign{ fonts::TextAlignment::Left };
        Property<bool> Ellipsis{ false };
        Property<Optional<f32>> FontSize;
        Property<String> FontFamily;
        Property<Optional<core::Color>> TextColor;
        Property<bool> DoubleClickToEdit{ true };
        Property<bool> SlowClickToEdit{ true };

        /// Optional validation: return true if the new name is acceptable.
        Function<bool(StringView)> ValidateRename;

        Event<void(EditableLabel*, StringView)> OnRenameCommitted;
        Event<void(EditableLabel*)> OnRenameCancelled;

        EditableLabel()
        {
            Cursor = CursorType::Arrow;
            IsReadOnly.SetValue(true);
            IsFocusable = false;
            IsTabStop = false;

            TextOffsetX.SetOwner(this);
            HAlign.SetOwner(this, InvalidationKind::Visual);
            Ellipsis.SetOwner(this, InvalidationKind::Visual);
            FontSize.SetOwner(this);
            FontFamily.SetOwner(this, InvalidationKind::Visual);
            TextColor.SetOwner(this, InvalidationKind::Visual);
            DoubleClickToEdit.SetOwner(this);
            SlowClickToEdit.SetOwner(this);
        }

        [[nodiscard]] bool IsEditing() const noexcept { return m_isEditing; }

        /// Set the display text (ignored while editing). Hides EditText::SetText.
        void SetText(StringView text)
        {
            if (m_isEditing) { return; }
            EditText::SetText(text);
        }

        /// Enter edit mode: select all, show cursor.
        void BeginEdit()
        {
            if (m_isEditing) { return; }
            m_isEditing = true;
            m_wasClickedOnce = false;
            m_preEditText = String(Text());
            IsReadOnly.SetValue(false);
            IsFocusable = true;
            IsTabStop = true;
            Cursor = CursorType::IBeam;

            if (Context != nullptr) { Context->GetFocusManager()->SetFocus(this); }
            Behavior().HandleKeyDown(KeyCode::A, KeyModifiers::Ctrl); // select all
        }

        /// Commit the edit and exit edit mode.
        void CommitEdit()
        {
            if (!m_isEditing) { return; }
            const StringView newText = Text();

            if (Trimmed(newText).IsEmpty()) { CancelEdit(); return; }              // reject empty/whitespace
            if (newText == m_preEditText.AsView()) { CancelEdit(); return; }        // reject unchanged
            if (ValidateRename && !ValidateRename(newText)) { CancelEdit(); return; } // custom validator

            m_isEditing = false;
            IsReadOnly.SetValue(true);
            IsFocusable = false;
            IsTabStop = false;
            Cursor = CursorType::Arrow;
            OnRenameCommitted.Invoke(this, newText);
        }

        /// Cancel the edit, restoring the original text.
        void CancelEdit()
        {
            if (!m_isEditing) { return; }
            m_isEditing = false;
            IsReadOnly.SetValue(true);
            IsFocusable = false;
            IsTabStop = false;
            Cursor = CursorType::Arrow;
            EditText::SetText(m_preEditText);
            OnRenameCancelled.Invoke(this);
        }

        void OnFocusLost() override
        {
            // Don't commit if focus was pushed to the stack for a popup.
            if (m_isEditing && Context != nullptr && Context->GetFocusManager()->FocusStackDepth() == 0) { CommitEdit(); }
            EditText::OnFocusLost();
        }

        void OnKeyDown(KeyEventArgs& e) override
        {
            if (m_isEditing)
            {
                if (e.Key == KeyCode::Return) { CommitEdit(); e.Handled = true; return; }
                if (e.Key == KeyCode::Escape) { CancelEdit(); e.Handled = true; return; }
                EditText::OnKeyDown(e);
                return;
            }
            // Not editing - don't handle keys.
        }

        void OnMouseDown(MouseEventArgs& e) override
        {
            if (m_isEditing) { EditText::OnMouseDown(e); return; }
            if (e.Button != MouseButton::Left) { return; }

            if (DoubleClickToEdit.Value() && e.ClickCount >= 2) { BeginEdit(); e.Handled = true; return; }

            if (SlowClickToEdit.Value() && e.ClickCount == 1)
            {
                const f32 now = Context ? Context->TotalTime() : 0.0f;
                if (m_wasClickedOnce)
                {
                    const f32 elapsed = now - m_lastClickTime;
                    if (elapsed > 0.4f && elapsed < 1.5f) { BeginEdit(); m_wasClickedOnce = false; e.Handled = true; return; }
                }
                m_wasClickedOnce = true;
                m_lastClickTime = now;
            }
            // Don't set e.Handled - let the parent handle selection.
        }

        void OnDraw(UIDrawContext& ctx) override
        {
            if (m_isEditing)
            {
                const Rectangle editBounds{ TextOffsetX.Value() - 2.0f, 0, Width() - TextOffsetX.Value() + 2.0f, Height() };
                if (Drawable* bg = ResolveStyleDrawable(StyleProperty::Background)) { bg->Draw(ctx, editBounds); }
                else { ctx.VG().FillRect(editBounds, Color{ 30.0f / 255.0f, 32.0f / 255.0f, 42.0f / 255.0f, 1.0f }); }

                const Color borderColor = ResolveStyleColor(StyleProperty::AccentColor, ResolveStyleColor(StyleProperty::CursorColor, Color{ 80.0f / 255.0f, 160.0f / 255.0f, 1.0f, 1.0f }));
                ctx.VG().StrokeRect(editBounds, borderColor, 1.0f);
                DrawEditContent(ctx, TextOffsetX.Value());
                return;
            }

            // Label mode: plain text with optional ellipsis.
            const StringView text = Text();
            const f32 fontSize = FontSize.Value().HasValue() ? FontSize.Value().Value() : ResolveStyleFloat(StyleProperty::FontSize, 14.0f);
            if (text.Size() == 0 || ctx.FontService() == nullptr) { return; }
            fonts::CachedFont* font = ctx.FontService()->GetFont(ResolveStyleFontFamily(FontFamily.Value()), fontSize);
            if (font == nullptr) { return; }

            const Color textColor = TextColor.Value().HasValue() ? TextColor.Value().Value() : ResolveStyleColor(StyleProperty::TextColor, Color{ 220.0f / 255.0f, 225.0f / 255.0f, 235.0f / 255.0f, 1.0f });
            const Rectangle textBounds{ TextOffsetX.Value(), 0, Width() - TextOffsetX.Value(), Height() };
            const fonts::TextAlignment h = HAlign.Value();

            if (Ellipsis.Value() && font->font->MeasureString(text) > textBounds.width)
            {
                const StringView ellipsis = u8"...";
                const f32 ellipsisW = font->font->MeasureString(ellipsis);
                const f32 availW = textBounds.width - ellipsisW;
                if (availW <= 0)
                {
                    ctx.VG().DrawText(ellipsis, font, textBounds, h, fonts::VerticalAlignment::Middle, textColor);
                }
                else
                {
                    String truncated;
                    usize i = 0;
                    while (i < text.Size())
                    {
                        const usize start = i;
                        const u32 cp = DecodeUtf8(text, i);
                        const usize prevSize = truncated.Size();
                        AppendUtf8(truncated, cp);
                        if (font->font->MeasureString(truncated) > availW) { truncated.Remove(prevSize, truncated.Size() - prevSize); break; }
                        (void)start;
                    }
                    truncated.Append(ellipsis);
                    ctx.VG().DrawText(truncated, font, textBounds, h, fonts::VerticalAlignment::Middle, textColor);
                }
            }
            else
            {
                ctx.VG().DrawText(text, font, textBounds, h, fonts::VerticalAlignment::Middle, textColor);
            }
        }

    private:
        void DrawEditContent(UIDrawContext& ctx, f32 offsetX)
        {
            const f32 fontSize = FontSize.Value().HasValue() ? FontSize.Value().Value() : ResolveStyleFloat(StyleProperty::FontSize, 14.0f);
            const f32 contentW = Width() - offsetX;
            ctx.VG().PushClipRect(Rectangle{ offsetX, 0, contentW, Height() });
            DrawTextContent(ctx, offsetX, 0, contentW, Height(), fontSize);
            ctx.VG().PopClip();
        }

        bool m_isEditing = false;
        String m_preEditText;
        f32 m_lastClickTime = 0.0f;
        bool m_wasClickedOnce = false;
    };

    DRACONIC_DEFINE_OBJECT(EditableLabel, "draconic::ui")
}
