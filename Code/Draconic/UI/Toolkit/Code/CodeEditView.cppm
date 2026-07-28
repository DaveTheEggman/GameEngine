// Draconic UI Toolkit - :code_edit_view partition
//
// CodeEditView (docs/design/code-editor.md): the purpose-built code editor widget over the
// :code_document core. Virtualized monospace rendering (only visible lines are drawn; column
// geometry is column * advance), a line-number gutter with clickable markers (breakpoints,
// diagnostics, execution line), full keyboard/mouse editing over CodeDocument's delta undo, and
// the completion seam: composable ICompletionProviders feed a CompletionModel whose popup is
// SELF-DRAWN inside this widget - deliberately NOT PopupLayer::ShowPopup, which pushes/clears
// focus; the editor must keep focus (and IME routing) while the popup routes its keys.
//
// The widget consumes keys it acts on and leaves everything else unhandled so application
// shortcuts (save, palette) keep working while the editor is focused. Tab requires the core
// WantsTabKey opt-in (dispatch-first, traversal fallback - see InputManager::ProcessKeyDown).

module;
#include <cstdio>
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include "Core/Reflection/Reflect.h"

export module draconic.ui.toolkit:code_edit_view;

import draconic.core;
import draconic.vg;
import draconic.fonts;
import draconic.ui;
import :code_document;

using namespace draconic::core;

export namespace draconic::ui::toolkit
{

    // ---- completion seam ---------------------------------------------------------------------

    struct CompletionCandidate
    {
        String label;      // shown in the popup, matched against the typed prefix
        String insertText; // replaces the prefix on accept (usually == label)
    };

    /// A completion source. Providers are COMPOSABLE: the view queries every registered provider
    /// and merges (dedupe by label, sorted). `prefix` is the identifier fragment left of the
    /// cursor at `cursor` (may be empty for explicit Ctrl+Space invocation).
    class ICompletionProvider
    {
    public:
        virtual ~ICompletionProvider() = default;
        virtual void Collect(const CodeDocument& document, CodePosition cursor, StringView prefix,
                             Array<CompletionCandidate>& out) = 0;
    };

    /// The provider every language gets for free: identifiers harvested from the buffer itself.
    class DocumentWordCompletionProvider final : public ICompletionProvider
    {
    public:
        void Collect(const CodeDocument& document, CodePosition cursor, StringView prefix,
                     Array<CompletionCandidate>& out) override
        {
            (void)cursor;
            const Span<const String> words = document.Words();
            for (usize i = 0; i < words.Size(); ++i)
            {
                // The fragment being typed is itself a harvested word - suggesting it back
                // verbatim is noise; longer words that extend it still match.
                if (words[i].AsView() == prefix)
                {
                    continue;
                }
                out.PushBack(CompletionCandidate{String(words[i].AsView()),
                                                 String(words[i].AsView())});
            }
        }
    };

    // ---- the popup model (headless: filter + key routing, no UI) -----------------------------

    enum class CompletionKeyResult : u8
    {
        Ignored,   // not a popup key - the editor handles it normally
        Consumed,  // popup navigation consumed the key
        Accepted,  // commit the selected candidate
        Dismissed, // popup closed; the editor should NOT process the key further
    };

    /// State + key routing for the completion popup. The view feeds keys here FIRST while open
    /// (this is why completion shapes the P1 input design); rendering stays in the view.
    class CompletionModel
    {
    public:
        void Open(CodePosition anchor, Array<CompletionCandidate> candidates, StringView prefix)
        {
            m_anchor = anchor;
            m_all = Move(candidates);
            m_open = true;
            Filter(prefix);
        }

        void Close() noexcept
        {
            m_open = false;
            m_all.Clear();
            m_filtered.Clear();
            m_selected = 0;
        }

        [[nodiscard]] bool IsOpen() const noexcept { return m_open; }
        [[nodiscard]] CodePosition Anchor() const noexcept { return m_anchor; }

        /// Re-filters against the (re)typed prefix: case-insensitive prefix match, exact-case
        /// matches ranked first (stable within each group). Closes when nothing matches, or when
        /// the only match IS the prefix (nothing left to complete).
        void Filter(StringView prefix)
        {
            if (!m_open)
            {
                return;
            }
            m_filtered.Clear();
            for (usize i = 0; i < m_all.Size(); ++i)
            {
                if (StartsWithCaseSensitive(m_all[i].label.AsView(), prefix))
                {
                    m_filtered.PushBack(static_cast<i32>(i));
                }
            }
            for (usize i = 0; i < m_all.Size(); ++i)
            {
                if (!StartsWithCaseSensitive(m_all[i].label.AsView(), prefix) &&
                    StartsWithCaseInsensitive(m_all[i].label.AsView(), prefix))
                {
                    m_filtered.PushBack(static_cast<i32>(i));
                }
            }
            m_selected = 0;
            if (m_filtered.IsEmpty() ||
                (m_filtered.Size() == 1 &&
                 m_all[static_cast<usize>(m_filtered[0])].label.AsView() == prefix))
            {
                Close();
            }
        }

        [[nodiscard]] CompletionKeyResult HandleKey(KeyCode key)
        {
            if (!m_open)
            {
                return CompletionKeyResult::Ignored;
            }
            switch (key)
            {
                case KeyCode::Up:
                    m_selected = m_selected > 0 ? m_selected - 1 : 0;
                    return CompletionKeyResult::Consumed;
                case KeyCode::Down:
                    m_selected = Min(m_selected + 1, static_cast<i32>(m_filtered.Size()) - 1);
                    return CompletionKeyResult::Consumed;
                case KeyCode::Return:
                case KeyCode::Tab:
                    return CompletionKeyResult::Accepted;
                case KeyCode::Escape:
                    Close();
                    return CompletionKeyResult::Dismissed;
                default:
                    return CompletionKeyResult::Ignored;
            }
        }

        [[nodiscard]] i32 ItemCount() const noexcept { return static_cast<i32>(m_filtered.Size()); }
        [[nodiscard]] i32 SelectedIndex() const noexcept { return m_selected; }
        void SetSelectedIndex(i32 index) noexcept
        {
            m_selected = Clamp(index, 0, Max(0, static_cast<i32>(m_filtered.Size()) - 1));
        }

        [[nodiscard]] const CompletionCandidate* Item(i32 index) const
        {
            if (index < 0 || index >= ItemCount())
            {
                return nullptr;
            }
            return &m_all[static_cast<usize>(m_filtered[static_cast<usize>(index)])];
        }
        [[nodiscard]] const CompletionCandidate* Selected() const { return Item(m_selected); }

    private:
        [[nodiscard]] static char8_t ToLowerAscii(char8_t c) noexcept
        {
            return (c >= u8'A' && c <= u8'Z') ? static_cast<char8_t>(c + 32) : c;
        }
        [[nodiscard]] static bool StartsWithCaseSensitive(StringView s, StringView prefix) noexcept
        {
            return s.StartsWith(prefix);
        }
        [[nodiscard]] static bool StartsWithCaseInsensitive(StringView s,
                                                            StringView prefix) noexcept
        {
            if (prefix.Size() > s.Size())
            {
                return false;
            }
            for (usize i = 0; i < prefix.Size(); ++i)
            {
                if (ToLowerAscii(s[i]) != ToLowerAscii(prefix[i]))
                {
                    return false;
                }
            }
            return true;
        }

        bool m_open = false;
        CodePosition m_anchor{};
        Array<CompletionCandidate> m_all;
        Array<i32> m_filtered; // indices into m_all
        i32 m_selected = 0;
    };

    // ---- the widget --------------------------------------------------------------------------

    class CodeEditView : public ViewGroup
    {
        DRACONIC_OBJECT(CodeEditView, ViewGroup)

    public:
        // Appearance/behavior knobs (plain fields, read each frame like other toolkit widgets).
        f32 FontSize = 13.0f;
        String FontFamily = String(u8"Mono"); // falls back to the default family when absent
        i32 TabWidth = 4;                     // spaces per indent step (tabs insert spaces)
        bool ShowGutter = true;
        bool ShowLineNumbers = true;
        bool ReadOnly = false;
        bool AllowBreakpoints = true;        // gutter margin click toggles Breakpoint markers
        bool DocumentWordCompletion = true;  // built-in identifier provider
        i32 AutoCompleteMinPrefix = 2;       // identifier chars typed before the popup auto-opens

        Event<void()> OnTextChanged;               // any document mutation (typing, undo, paste)
        Event<void(i32, bool)> OnBreakpointToggled; // (line, nowSet) after a gutter toggle

        CodeEditView()
        {
            IsFocusable = true;
            IsTabStop = true;
            WantsArrowKeys = true;
            WantsTabKey = true;
            ClipsContent = true;
            Cursor = CursorType::IBeam;

            CodeEditView* self = this;
            m_vBar = MakeRef<ScrollBar>(DefaultAllocator(), false);
            m_vBar->Parent = this;
            m_vBar->OnValueChanged.Add(Event<void(ScrollBar*, f32)>::Handler{
                [self](ScrollBar*, f32 value)
                {
                    self->m_scrollY = value;
                    self->Invalidate();
                }});
            m_hBar = MakeRef<ScrollBar>(DefaultAllocator(), true);
            m_hBar->Parent = this;
            m_hBar->OnValueChanged.Add(Event<void(ScrollBar*, f32)>::Handler{
                [self](ScrollBar*, f32 value)
                {
                    self->m_scrollX = value;
                    self->Invalidate();
                }});

            m_doc.OnLinesChanged = [self](i32, i32, i32) { self->m_maxLineDirty = true; };
        }

        [[nodiscard]] bool WantsTextInput() const override
        {
            return IsEffectivelyEnabled() && !ReadOnly;
        }

        // ---- document access ----

        [[nodiscard]] CodeDocument& Document() noexcept { return m_doc; }
        [[nodiscard]] const CodeDocument& Document() const noexcept { return m_doc; }

        [[nodiscard]] String Text() const { return m_doc.Text(); }
        void SetText(StringView text)
        {
            m_doc.SetText(text);
            m_cursor = CodePosition{};
            m_anchor = CodePosition{};
            m_desiredColumn = -1;
            m_scrollX = 0;
            m_scrollY = 0;
            m_completion.Close();
            m_maxLineDirty = true;
            Invalidate();
        }

        [[nodiscard]] CodePosition CursorPosition() const noexcept { return m_cursor; }
        void SetCursorPosition(CodePosition pos)
        {
            m_cursor = m_doc.ClampPosition(pos);
            m_anchor = m_cursor;
            m_desiredColumn = -1;
            m_pendingCursorScroll = true;
            ResetBlink();
            Invalidate();
        }

        [[nodiscard]] bool HasSelection() const noexcept { return !(m_cursor == m_anchor); }
        [[nodiscard]] CodeSpan Selection() const noexcept
        {
            return CodeSpan{m_anchor, m_cursor}.Normalized();
        }
        [[nodiscard]] String SelectedText() const { return m_doc.TextInSpan(Selection()); }

        void SelectAll()
        {
            m_anchor = CodePosition{};
            m_cursor = m_doc.EndPosition();
            Invalidate();
        }

        [[nodiscard]] f32 ScrollY() const noexcept { return m_scrollY; }
        [[nodiscard]] f32 ScrollX() const noexcept { return m_scrollX; }

        /// Scrolls so `line` is visible (roughly centered) and places the cursor on it.
        void ScrollToLine(i32 line)
        {
            line = Clamp(line, 0, m_doc.LineCount() - 1);
            SetCursorPosition(CodePosition{line, 0});
            m_scrollY = Max(0.0f, static_cast<f32>(line) * LineHeight() - m_viewportH * 0.5f);
            ClampScroll();
            Invalidate();
        }

        // ---- completion ----

        /// Registers a provider (borrowed; caller keeps it alive while registered).
        void AddCompletionProvider(ICompletionProvider* provider)
        {
            if (provider != nullptr)
            {
                m_providers.PushBack(provider);
            }
        }

        [[nodiscard]] CompletionModel& Completion() noexcept { return m_completion; }

        /// Opens the popup at the current word (explicit Ctrl+Space path; also used by tests).
        void RequestCompletion() { OpenCompletion(true); }

        // ---- metrics (fallbacks keep headless tests working without a font service) ----

        [[nodiscard]] f32 LineHeight() const noexcept
        {
            return m_lineHeight > 0.0f ? m_lineHeight : FontSize * 1.35f;
        }
        [[nodiscard]] f32 ColumnAdvance() const noexcept
        {
            return m_advance > 0.0f ? m_advance : FontSize * 0.6f;
        }
        [[nodiscard]] f32 GutterWidth() const
        {
            if (!ShowGutter)
            {
                return 0.0f;
            }
            f32 digitsWidth = 0.0f;
            if (ShowLineNumbers)
            {
                i32 digits = 1;
                for (i32 n = m_doc.LineCount(); n >= 10; n /= 10)
                {
                    ++digits;
                }
                digitsWidth = static_cast<f32>(Max(digits, 3)) * ColumnAdvance() + kNumberPad;
            }
            return kMarkerMargin + digitsWidth + kGutterGap;
        }

        /// Buffer position for a point in view-local coordinates.
        [[nodiscard]] CodePosition PositionAt(f32 localX, f32 localY) const
        {
            const i32 line = static_cast<i32>((localY + m_scrollY - kPadTop) / LineHeight());
            const f32 textX = localX - (GutterWidth() + kPadLeft) + m_scrollX;
            const i32 column = static_cast<i32>((textX / ColumnAdvance()) + 0.5f);
            return m_doc.ClampPosition(CodePosition{line, column});
        }

        // ---- input ----

        void OnKeyDown(KeyEventArgs& e) override
        {
            if (!IsEffectivelyEnabled())
            {
                return;
            }

            // The popup routes its keys FIRST while open - without stealing focus.
            const CompletionKeyResult routed = m_completion.HandleKey(e.Key);
            if (routed == CompletionKeyResult::Accepted)
            {
                AcceptCompletion();
                e.Handled = true;
                return;
            }
            if (routed == CompletionKeyResult::Consumed || routed == CompletionKeyResult::Dismissed)
            {
                Invalidate();
                e.Handled = true;
                return;
            }

            if (ProcessKey(e.Key, e.Modifiers))
            {
                e.Handled = true;
            }
        }

        void OnTextInput(TextInputEventArgs& e) override
        {
            if (!IsEffectivelyEnabled() || ReadOnly || e.Character < 0x20 || e.Character == 0x7F)
            {
                return;
            }
            String text;
            AppendUtf8(text, static_cast<u32>(e.Character));
            InsertText(text.AsView(), CodeEditKind::Typing);

            // Completion: refilter while open; auto-open once the identifier fragment is long
            // enough (word chars only - punctuation closes below via the empty prefix).
            const StringView prefix = PrefixView();
            if (m_completion.IsOpen())
            {
                if (m_cursor.line != m_completion.Anchor().line || prefix.IsEmpty())
                {
                    m_completion.Close();
                }
                else
                {
                    m_completion.Filter(prefix);
                }
            }
            else if (static_cast<i32>(Utf8Length(prefix)) >= AutoCompleteMinPrefix)
            {
                OpenCompletion(false);
            }
            e.Handled = true;
        }

        void OnMouseDown(MouseEventArgs& e) override
        {
            if (!IsEffectivelyEnabled())
            {
                return;
            }
            if (Context != nullptr)
            {
                Context->GetFocusManager()->SetFocus(this);
            }
            m_completion.Close();
            if (e.Button != MouseButton::Left)
            {
                return;
            }

            // Marker-margin click toggles a breakpoint on that line.
            if (ShowGutter && AllowBreakpoints && e.X < kMarkerMargin)
            {
                const i32 line =
                    static_cast<i32>((e.Y + m_scrollY - kPadTop) / LineHeight());
                if (line >= 0 && line < m_doc.LineCount())
                {
                    const bool set = m_doc.ToggleMarker(line, CodeMarkers::Breakpoint);
                    OnBreakpointToggled.Invoke(line, set);
                    Invalidate();
                }
                e.Handled = true;
                return;
            }

            const CodePosition pos = PositionAt(e.X, e.Y);
            if (e.ClickCount >= 3)
            {
                m_anchor = CodePosition{pos.line, 0};
                m_cursor = pos.line + 1 < m_doc.LineCount()
                               ? CodePosition{pos.line + 1, 0}
                               : m_doc.EndPosition();
            }
            else if (e.ClickCount == 2)
            {
                const CodeSpan word = m_doc.WordAt(pos);
                m_anchor = word.begin;
                m_cursor = word.end;
            }
            else
            {
                if (!HasFlag(e.Modifiers, KeyModifiers::Shift))
                {
                    m_anchor = pos;
                }
                m_cursor = pos;
                m_dragging = true;
                if (Context != nullptr)
                {
                    Context->GetFocusManager()->SetCapture(this);
                }
            }
            m_desiredColumn = -1;
            ResetBlink();
            Invalidate();
            e.Handled = true;
        }

        void OnMouseMove(MouseEventArgs& e) override
        {
            if (!m_dragging)
            {
                return;
            }
            m_cursor = PositionAt(e.X, e.Y);
            m_pendingCursorScroll = true;
            Invalidate();
            e.Handled = true;
        }

        void OnMouseUp(MouseEventArgs& e) override
        {
            if (m_dragging)
            {
                m_dragging = false;
                if (Context != nullptr)
                {
                    Context->GetFocusManager()->ReleaseCapture();
                }
                e.Handled = true;
            }
        }

        void OnMouseWheel(MouseWheelEventArgs& e) override
        {
            const bool horizontal = HasFlag(e.Modifiers, KeyModifiers::Shift);
            if (horizontal)
            {
                m_scrollX -= e.DeltaY * ColumnAdvance() * 6.0f;
            }
            else
            {
                m_scrollY -= e.DeltaY * LineHeight() * 3.0f;
            }
            ClampScroll();
            Invalidate();
            e.Handled = true;
        }

        void OnFocusGained() override { ResetBlink(); }
        void OnFocusLost() override
        {
            m_dragging = false;
            m_completion.Close();
            m_doc.BreakUndoChain();
            Invalidate();
        }

        // ---- layout ----

        void OnMeasure(BoxConstraints constraints) override
        {
            MeasuredSize = Float2{constraints.ConstrainWidth(480.0f),
                                  constraints.ConstrainHeight(320.0f)};
        }

        void OnLayout(f32 left, f32 top, f32 width, f32 height) override
        {
            (void)left;
            (void)top;
            RefreshMetrics();
            RefreshMaxLine();

            const f32 barSize = m_vBar->BarThickness;
            const bool needV = ContentHeight() > height;
            const bool needH = ContentWidth() > width - (needV ? barSize : 0.0f) - GutterWidth();
            m_viewportH = height - (needH ? barSize : 0.0f);
            m_viewportW = width - (needV ? barSize : 0.0f);

            if (m_pendingCursorScroll)
            {
                EnsureCursorVisible();
                m_pendingCursorScroll = false;
            }
            ClampScroll();

            if (Context != nullptr && m_vBar->Context == nullptr)
            {
                Context->AttachView(m_vBar.Get());
            }
            if (Context != nullptr && m_hBar->Context == nullptr)
            {
                Context->AttachView(m_hBar.Get());
            }

            m_vBar->Visibility = needV ? VisibilityValue::Visible : VisibilityValue::Gone;
            m_hBar->Visibility = needH ? VisibilityValue::Visible : VisibilityValue::Gone;
            // Max BEFORE value: SetValue clamps against the bar's current max AND fires
            // OnValueChanged on a clamp - with a stale (smaller) max it would snap m_scrollY
            // back a line right after EnsureCursorVisible advanced it (the half-hidden
            // caret-line bug from the first smoke run).
            if (needV)
            {
                m_vBar->SetMaxValue(MaxScrollY());
                m_vBar->SetViewportSize(m_viewportH);
                m_vBar->SetValue(m_scrollY);
                m_vBar->Measure(BoxConstraints::Tight(barSize, m_viewportH));
                m_vBar->Layout(width - barSize, 0, barSize, m_viewportH);
            }
            if (needH)
            {
                m_hBar->SetMaxValue(MaxScrollX());
                m_hBar->SetViewportSize(m_viewportW - GutterWidth());
                m_hBar->SetValue(m_scrollX);
                m_hBar->Measure(BoxConstraints::Tight(m_viewportW, barSize));
                m_hBar->Layout(0, height - barSize, m_viewportW, barSize);
            }
        }

        [[nodiscard]] usize VisualChildCount() const override { return ChildCount() + 2; }
        [[nodiscard]] View* GetVisualChild(usize index) const override
        {
            if (index < ChildCount())
            {
                return ViewGroup::GetVisualChild(index);
            }
            if (index == ChildCount())
            {
                return m_vBar.Get();
            }
            if (index == ChildCount() + 1)
            {
                return m_hBar.Get();
            }
            return nullptr;
        }

        // ---- drawing ----

        void OnDraw(UIDrawContext& ctx) override
        {
            RefreshMetrics(ctx.FontService());
            fonts::CachedFont* font =
                ctx.FontService() != nullptr
                    ? ctx.FontService()->GetFont(FontFamily.AsView(), FontSize)
                    : nullptr;

            const f32 width = Width();
            const f32 height = Height();
            const f32 lineH = LineHeight();
            const f32 advance = ColumnAdvance();
            const f32 gutterW = GutterWidth();
            const f32 textLeft = gutterW + kPadLeft - m_scrollX;
            const f32 ascent = font != nullptr ? font->font->Metrics().ascent : lineH * 0.8f;

            const Color background =
                ResolveStyleColor(StyleProperty::Background, Rgb(26, 28, 34));
            const Color textColor =
                ResolveStyleColor(StyleProperty::TextColor, Rgb(220, 225, 235));
            const Color dimColor =
                ResolveStyleColor(StyleProperty::TextDimColor, Rgb(120, 128, 144));
            const Color selectionColor =
                ResolveStyleColor(StyleProperty::SelectionColor, Rgb(60, 120, 200, 80));
            const Color cursorColor =
                ResolveStyleColor(StyleProperty::CursorColor, Rgb(220, 225, 235));
            const Color accent = ResolveStyleColor(StyleProperty::AccentColor, Rgb(230, 140, 60));

            ctx.VG().FillRect(Rectangle{0, 0, width, height}, background);

            const i32 firstLine =
                Max(0, static_cast<i32>((m_scrollY - kPadTop) / lineH));
            const i32 lastLine = Min(m_doc.LineCount() - 1,
                                     static_cast<i32>((m_scrollY + m_viewportH) / lineH) + 1);
            const CodeSpan selection = Selection();

            // Text region, CLIPPED to the right of the gutter: horizontally scrolled text
            // must never bleed under the gutter (first smoke-run finding). The gutter itself
            // is painted afterwards, on top.
            ctx.PushClip(Rectangle{gutterW, 0, width - gutterW, height});
            for (i32 line = firstLine; line <= lastLine; ++line)
            {
                const f32 lineTop = kPadTop + static_cast<f32>(line) * lineH - m_scrollY;
                const CodeMarkers markers = m_doc.MarkersOn(line);

                // Row highlights: execution line wins over the quiet current-line tint.
                if (HasMarker(markers, CodeMarkers::ExecutionLine))
                {
                    ctx.VG().FillRect(Rectangle{gutterW, lineTop, width - gutterW, lineH},
                                      WithAlpha(accent, 0.18f));
                }
                else if (line == m_cursor.line && !HasSelection() && IsFocused())
                {
                    ctx.VG().FillRect(Rectangle{gutterW, lineTop, width - gutterW, lineH},
                                      WithAlpha(textColor, 0.05f));
                }
                if (HasMarker(markers, CodeMarkers::Error))
                {
                    ctx.VG().FillRect(Rectangle{gutterW, lineTop, width - gutterW, lineH},
                                      Rgb(200, 70, 70, 26));
                }

                // Selection band(s).
                if (HasSelection() && line >= selection.begin.line && line <= selection.end.line)
                {
                    const i32 fromCol =
                        line == selection.begin.line ? selection.begin.column : 0;
                    const f32 toCol =
                        line == selection.end.line
                            ? static_cast<f32>(selection.end.column)
                            : static_cast<f32>(m_doc.LineLength(line)) + 0.4f; // show the newline
                    ctx.VG().FillRect(
                        Rectangle{textLeft + static_cast<f32>(fromCol) * advance, lineTop,
                                  (toCol - static_cast<f32>(fromCol)) * advance, lineH},
                        selectionColor);
                }

                // The text itself.
                if (font != nullptr && !m_doc.Line(line).IsEmpty())
                {
                    ctx.VG().DrawText(m_doc.Line(line), font,
                                      Float2{textLeft, lineTop + ascent}, textColor);
                }
            }

            // Caret (clipped with the text region).
            if (IsFocused() && !ReadOnly)
            {
                const f32 elapsed =
                    (Context != nullptr ? Context->TotalTime() : 0.0f) - m_blinkReset;
                if ((static_cast<i32>(elapsed / 0.5f) % 2) == 0)
                {
                    const f32 caretX =
                        textLeft + static_cast<f32>(m_cursor.column) * advance;
                    const f32 caretY =
                        kPadTop + static_cast<f32>(m_cursor.line) * lineH - m_scrollY;
                    ctx.VG().FillRect(Rectangle{caretX - 1.0f, caretY, 2.0f, lineH}, cursorColor);
                }
            }
            ctx.PopClip();

            // The gutter: painted AFTER (over) the text region so nothing bleeds into it.
            if (ShowGutter)
            {
                ctx.VG().FillRect(Rectangle{0, 0, gutterW, height},
                                  Palette::Darken(background, 0.25f));
                for (i32 line = firstLine; line <= lastLine; ++line)
                {
                    const f32 lineTop = kPadTop + static_cast<f32>(line) * lineH - m_scrollY;
                    const CodeMarkers markers = m_doc.MarkersOn(line);
                    const f32 centerY = lineTop + lineH * 0.5f;
                    if (HasMarker(markers, CodeMarkers::Breakpoint))
                    {
                        ctx.VG().FillCircle(Float2{kMarkerMargin * 0.5f, centerY}, 4.5f,
                                            Rgb(214, 80, 80));
                    }
                    if (HasMarker(markers, CodeMarkers::ExecutionLine))
                    {
                        // Right-pointing arrow in the margin.
                        ctx.VG().BeginPath();
                        ctx.VG().MoveTo(kMarkerMargin * 0.5f - 4.0f, centerY - 4.5f);
                        ctx.VG().LineTo(kMarkerMargin * 0.5f + 4.0f, centerY);
                        ctx.VG().LineTo(kMarkerMargin * 0.5f - 4.0f, centerY + 4.5f);
                        ctx.VG().Stroke(Rgb(240, 200, 90), 2.0f);
                    }
                    else if (HasMarker(markers, CodeMarkers::Error) ||
                             HasMarker(markers, CodeMarkers::Warning))
                    {
                        const Color c = HasMarker(markers, CodeMarkers::Error)
                                            ? Rgb(214, 80, 80)
                                            : Rgb(240, 200, 90);
                        ctx.VG().FillRect(
                            Rectangle{kMarkerMargin - 5.0f, centerY - 3.5f, 7.0f, 7.0f}, c);
                    }
                    if (ShowLineNumbers && font != nullptr)
                    {
                        char buffer[16];
                        const int n = std::snprintf(buffer, sizeof(buffer), "%d", line + 1);
                        const StringView number(reinterpret_cast<const char8_t*>(buffer),
                                                n > 0 ? static_cast<usize>(n) : 0u);
                        const f32 numberRight = gutterW - kGutterGap;
                        const f32 numberX =
                            numberRight - static_cast<f32>(number.Size()) * advance;
                        const Color numberColor =
                            line == m_cursor.line ? textColor : dimColor;
                        ctx.VG().DrawText(number, font, Float2{numberX, lineTop + ascent},
                                          numberColor);
                    }
                }
            }

            DrawChildren(ctx); // scrollbars

            if (m_completion.IsOpen())
            {
                DrawCompletionPopup(ctx, font, textLeft, lineH, advance, ascent, background,
                                    textColor, accent);
            }
        }

    private:
        static constexpr f32 kPadTop = 4.0f;
        static constexpr f32 kPadLeft = 6.0f;
        static constexpr f32 kMarkerMargin = 18.0f;
        static constexpr f32 kNumberPad = 4.0f;
        static constexpr f32 kGutterGap = 8.0f;
        static constexpr i32 kPopupMaxVisible = 8;

        [[nodiscard]] static Color Rgb(u8 r, u8 g, u8 b, u8 a = 255) noexcept
        {
            return Color{static_cast<f32>(r) / 255.0f, static_cast<f32>(g) / 255.0f,
                         static_cast<f32>(b) / 255.0f, static_cast<f32>(a) / 255.0f};
        }
        [[nodiscard]] static Color WithAlpha(Color c, f32 a) noexcept
        {
            c.a = a;
            return c;
        }

        [[nodiscard]] f32 Now() const noexcept
        {
            return Context != nullptr ? Context->TotalTime() : 0.0f;
        }
        void ResetBlink() noexcept { m_blinkReset = Now(); }

        // ---- metrics ----

        void RefreshMetrics() { RefreshMetrics(Context != nullptr ? Context->FontService() : nullptr); }
        void RefreshMetrics(fonts::IFontService* service)
        {
            if (service == nullptr)
            {
                return;
            }
            fonts::CachedFont* font = service->GetFont(FontFamily.AsView(), FontSize);
            if (font == nullptr || font->font == nullptr)
            {
                return;
            }
            m_lineHeight = font->font->Metrics().lineHeight;
            const fonts::GlyphInfo info = font->font->GetGlyphInfo('M');
            m_advance = info.advanceWidth > 0.0f ? info.advanceWidth
                                                 : font->font->MeasureString(StringView(u8"M"));
        }

        void RefreshMaxLine()
        {
            if (!m_maxLineDirty)
            {
                return;
            }
            m_maxLineLength = 0;
            for (i32 line = 0; line < m_doc.LineCount(); ++line)
            {
                m_maxLineLength = Max(m_maxLineLength, m_doc.LineLength(line));
            }
            m_maxLineDirty = false;
        }

        [[nodiscard]] f32 ContentHeight() const
        {
            return kPadTop * 2.0f + static_cast<f32>(m_doc.LineCount()) * LineHeight();
        }
        [[nodiscard]] f32 ContentWidth() const
        {
            return kPadLeft * 2.0f + static_cast<f32>(m_maxLineLength + 1) * ColumnAdvance();
        }
        [[nodiscard]] f32 MaxScrollY() const
        {
            return Max(0.0f, ContentHeight() - m_viewportH);
        }
        [[nodiscard]] f32 MaxScrollX() const
        {
            return Max(0.0f, ContentWidth() - (m_viewportW - GutterWidth()));
        }
        void ClampScroll()
        {
            m_scrollY = Clamp(m_scrollY, 0.0f, MaxScrollY());
            m_scrollX = Clamp(m_scrollX, 0.0f, MaxScrollX());
        }

        void EnsureCursorVisible()
        {
            const f32 lineH = LineHeight();
            const f32 caretTop = kPadTop + static_cast<f32>(m_cursor.line) * lineH;
            if (caretTop - kPadTop < m_scrollY)
            {
                m_scrollY = caretTop - kPadTop;
            }
            else if (caretTop + lineH > m_scrollY + m_viewportH)
            {
                m_scrollY = caretTop + lineH - m_viewportH;
            }
            const f32 caretX = static_cast<f32>(m_cursor.column) * ColumnAdvance();
            const f32 textViewportW = m_viewportW - GutterWidth() - kPadLeft * 2.0f;
            if (caretX < m_scrollX)
            {
                m_scrollX = Max(0.0f, caretX - ColumnAdvance() * 4.0f);
            }
            else if (caretX > m_scrollX + textViewportW)
            {
                m_scrollX = caretX - textViewportW + ColumnAdvance() * 4.0f;
            }
        }

        // ---- editing core ----

        void AfterEdit()
        {
            m_desiredColumn = -1;
            m_pendingCursorScroll = true;
            m_maxLineDirty = true;
            ResetBlink();
            Invalidate();
            OnTextChanged.Invoke();
        }

        void InsertText(StringView text, CodeEditKind kind)
        {
            const CodeCursorState before{m_cursor, m_anchor};
            m_cursor = m_doc.Edit(Selection(), text, kind, before, Now());
            m_anchor = m_cursor;
            AfterEdit();
        }

        void DeleteSpan(const CodeSpan& span, CodeEditKind kind)
        {
            const CodeCursorState before{m_cursor, m_anchor};
            m_cursor = m_doc.Edit(span, StringView(u8""), kind, before, Now());
            m_anchor = m_cursor;
            AfterEdit();
        }

        [[nodiscard]] CodePosition LeftOf(CodePosition pos) const
        {
            if (pos.column > 0)
            {
                return CodePosition{pos.line, pos.column - 1};
            }
            if (pos.line > 0)
            {
                return CodePosition{pos.line - 1, m_doc.LineLength(pos.line - 1)};
            }
            return pos;
        }
        [[nodiscard]] CodePosition RightOf(CodePosition pos) const
        {
            if (pos.column < m_doc.LineLength(pos.line))
            {
                return CodePosition{pos.line, pos.column + 1};
            }
            if (pos.line + 1 < m_doc.LineCount())
            {
                return CodePosition{pos.line + 1, 0};
            }
            return pos;
        }

        void MoveCursor(CodePosition pos, bool extendSelection)
        {
            m_cursor = m_doc.ClampPosition(pos);
            if (!extendSelection)
            {
                m_anchor = m_cursor;
            }
            m_doc.BreakUndoChain();
            m_completion.Close();
            m_pendingCursorScroll = true;
            ResetBlink();
            Invalidate();
        }

        [[nodiscard]] i32 FirstNonSpaceColumn(i32 line) const
        {
            const StringView text = m_doc.Line(line);
            i32 column = 0;
            usize i = 0;
            while (i < text.Size() && (text[i] == u8' ' || text[i] == u8'\t'))
            {
                ++i;
                ++column;
            }
            return column;
        }

        /// The identifier fragment immediately left of the cursor (completion prefix).
        [[nodiscard]] StringView PrefixView() const
        {
            const CodeSpan word = m_doc.WordAt(m_cursor);
            if (word.IsEmpty() || word.begin.line != m_cursor.line ||
                word.begin.column >= m_cursor.column)
            {
                return StringView();
            }
            const usize fromByte = m_doc.ColumnToByte(m_cursor.line, word.begin.column);
            const usize toByte = m_doc.ColumnToByte(m_cursor.line, m_cursor.column);
            return m_doc.Line(m_cursor.line).SubStr(fromByte, toByte - fromByte);
        }

        void OpenCompletion(bool explicitRequest)
        {
            const StringView prefix = PrefixView();
            if (!explicitRequest && prefix.IsEmpty())
            {
                return;
            }
            Array<CompletionCandidate> merged;
            if (DocumentWordCompletion)
            {
                m_wordProvider.Collect(m_doc, m_cursor, prefix, merged);
            }
            for (usize i = 0; i < m_providers.Size(); ++i)
            {
                m_providers[i]->Collect(m_doc, m_cursor, prefix, merged);
            }
            DedupeAndSort(merged);
            if (merged.IsEmpty())
            {
                m_completion.Close();
                return;
            }
            const CodePosition anchor{m_cursor.line,
                                      m_cursor.column - static_cast<i32>(Utf8Length(prefix))};
            m_completion.Open(anchor, Move(merged), prefix);
            Invalidate();
        }

        void AcceptCompletion()
        {
            const CompletionCandidate* candidate = m_completion.Selected();
            if (candidate == nullptr)
            {
                m_completion.Close();
                return;
            }
            const String insert = String(candidate->insertText.AsView());
            const CodeSpan replaced{m_completion.Anchor(), m_cursor};
            m_completion.Close();
            const CodeCursorState before{m_cursor, m_anchor};
            m_cursor = m_doc.Edit(replaced, insert.AsView(), CodeEditKind::Other, before, Now());
            m_anchor = m_cursor;
            AfterEdit();
        }

        static void DedupeAndSort(Array<CompletionCandidate>& items)
        {
            // Insertion sort by label (candidate lists are small); drop exact duplicates.
            for (usize i = 1; i < items.Size(); ++i)
            {
                CompletionCandidate value = Move(items[i]);
                usize j = i;
                while (j > 0 && Less(value.label.AsView(), items[j - 1].label.AsView()))
                {
                    items[j] = Move(items[j - 1]);
                    --j;
                }
                items[j] = Move(value);
            }
            usize write = 0;
            for (usize i = 0; i < items.Size(); ++i)
            {
                if (write == 0 || !(items[write - 1].label.AsView() == items[i].label.AsView()))
                {
                    if (write != i)
                    {
                        items[write] = Move(items[i]);
                    }
                    ++write;
                }
            }
            while (items.Size() > write)
            {
                items.PopBack();
            }
        }

        [[nodiscard]] static bool Less(StringView a, StringView b) noexcept
        {
            const usize n = Min(a.Size(), b.Size());
            for (usize i = 0; i < n; ++i)
            {
                if (a[i] != b[i])
                {
                    return a[i] < b[i];
                }
            }
            return a.Size() < b.Size();
        }

        // ---- key handling (returns true when the key was consumed) ----

        bool ProcessKey(KeyCode key, KeyModifiers mods)
        {
            const bool shift = HasFlag(mods, KeyModifiers::Shift);
            const bool ctrl = HasFlag(mods, KeyModifiers::Ctrl);
            const bool alt = HasFlag(mods, KeyModifiers::Alt);

            switch (key)
            {
                // -- navigation --
                case KeyCode::Left:
                    if (HasSelection() && !shift && !ctrl)
                    {
                        MoveCursor(Selection().begin, false);
                    }
                    else
                    {
                        MoveCursor(ctrl ? m_doc.PrevWordBoundary(m_cursor) : LeftOf(m_cursor),
                                   shift);
                    }
                    return true;
                case KeyCode::Right:
                    if (HasSelection() && !shift && !ctrl)
                    {
                        MoveCursor(Selection().end, false);
                    }
                    else
                    {
                        MoveCursor(ctrl ? m_doc.NextWordBoundary(m_cursor) : RightOf(m_cursor),
                                   shift);
                    }
                    return true;
                case KeyCode::Up:
                case KeyCode::Down:
                {
                    if (alt && !ReadOnly)
                    {
                        MoveLine(key == KeyCode::Down);
                        return true;
                    }
                    const i32 delta = key == KeyCode::Down ? 1 : -1;
                    const i32 targetLine = m_cursor.line + delta;
                    if (targetLine < 0 || targetLine >= m_doc.LineCount())
                    {
                        MoveCursor(delta < 0 ? CodePosition{0, 0} : m_doc.EndPosition(), shift);
                        return true;
                    }
                    if (m_desiredColumn < 0)
                    {
                        m_desiredColumn = m_cursor.column;
                    }
                    const i32 keepColumn = m_desiredColumn;
                    MoveCursor(CodePosition{targetLine, keepColumn}, shift);
                    m_desiredColumn = keepColumn; // MoveCursor clamps; keep the goal column
                    return true;
                }
                case KeyCode::Home:
                {
                    if (ctrl)
                    {
                        MoveCursor(CodePosition{0, 0}, shift);
                        return true;
                    }
                    // Smart home: first non-space, then hard column 0.
                    const i32 indent = FirstNonSpaceColumn(m_cursor.line);
                    MoveCursor(CodePosition{m_cursor.line,
                                            m_cursor.column == indent ? 0 : indent},
                               shift);
                    return true;
                }
                case KeyCode::End:
                    MoveCursor(ctrl ? m_doc.EndPosition()
                                    : CodePosition{m_cursor.line,
                                                   m_doc.LineLength(m_cursor.line)},
                               shift);
                    return true;
                case KeyCode::PageUp:
                case KeyCode::PageDown:
                {
                    const i32 page =
                        Max(1, static_cast<i32>(m_viewportH / LineHeight()) - 1);
                    const i32 delta = key == KeyCode::PageDown ? page : -page;
                    MoveCursor(CodePosition{m_cursor.line + delta, m_cursor.column}, shift);
                    return true;
                }

                // -- edits --
                case KeyCode::Return:
                    if (ReadOnly)
                    {
                        return false;
                    }
                    {
                        // Plain newline + copy the current line's leading whitespace.
                        String insert;
                        insert.PushBack(u8'\n');
                        const StringView line = m_doc.Line(m_cursor.line);
                        const usize indentBytes =
                            m_doc.ColumnToByte(m_cursor.line,
                                               Min(FirstNonSpaceColumn(m_cursor.line),
                                                   m_cursor.column));
                        insert.Append(line.SubStr(0, indentBytes));
                        InsertText(insert.AsView(), CodeEditKind::Newline);
                    }
                    return true;
                case KeyCode::Backspace:
                    if (ReadOnly)
                    {
                        return false;
                    }
                    if (HasSelection())
                    {
                        DeleteSpan(Selection(), CodeEditKind::Backspace);
                    }
                    else
                    {
                        const CodePosition from =
                            ctrl ? m_doc.PrevWordBoundary(m_cursor) : LeftOf(m_cursor);
                        if (!(from == m_cursor))
                        {
                            DeleteSpan(CodeSpan{from, m_cursor}, CodeEditKind::Backspace);
                        }
                    }
                    if (m_completion.IsOpen())
                    {
                        const StringView prefix = PrefixView();
                        if (prefix.IsEmpty())
                        {
                            m_completion.Close();
                        }
                        else
                        {
                            m_completion.Filter(prefix);
                        }
                    }
                    return true;
                case KeyCode::Delete:
                    if (ReadOnly)
                    {
                        return false;
                    }
                    if (HasSelection())
                    {
                        DeleteSpan(Selection(), CodeEditKind::Delete);
                    }
                    else
                    {
                        const CodePosition to =
                            ctrl ? m_doc.NextWordBoundary(m_cursor) : RightOf(m_cursor);
                        if (!(to == m_cursor))
                        {
                            DeleteSpan(CodeSpan{m_cursor, to}, CodeEditKind::Delete);
                        }
                    }
                    return true;
                case KeyCode::Tab:
                    if (ReadOnly)
                    {
                        return false;
                    }
                    HandleTab(shift);
                    return true;
                case KeyCode::Escape:
                    if (HasSelection())
                    {
                        m_anchor = m_cursor;
                        Invalidate();
                        return true;
                    }
                    return false;

                // -- chords --
                case KeyCode::A:
                    if (ctrl)
                    {
                        SelectAll();
                        return true;
                    }
                    return false;
                case KeyCode::C:
                    if (ctrl)
                    {
                        CopySelection(false);
                        return true;
                    }
                    return false;
                case KeyCode::X:
                    if (ctrl)
                    {
                        CopySelection(!ReadOnly);
                        return true;
                    }
                    return false;
                case KeyCode::V:
                    if (ctrl && !ReadOnly)
                    {
                        Paste();
                        return true;
                    }
                    return false;
                case KeyCode::Z:
                    if (ctrl && !ReadOnly)
                    {
                        CodeCursorState state;
                        if (shift ? m_doc.Redo(state) : m_doc.Undo(state))
                        {
                            m_cursor = m_doc.ClampPosition(state.cursor);
                            m_anchor = m_doc.ClampPosition(state.anchor);
                            AfterEdit();
                        }
                        return true;
                    }
                    return false;
                case KeyCode::Y:
                    if (ctrl && !ReadOnly)
                    {
                        CodeCursorState state;
                        if (m_doc.Redo(state))
                        {
                            m_cursor = m_doc.ClampPosition(state.cursor);
                            m_anchor = m_doc.ClampPosition(state.anchor);
                            AfterEdit();
                        }
                        return true;
                    }
                    return false;
                case KeyCode::D:
                    if (ctrl && !ReadOnly)
                    {
                        DuplicateLine();
                        return true;
                    }
                    return false;
                case KeyCode::Space:
                    if (ctrl)
                    {
                        OpenCompletion(true);
                        return true;
                    }
                    return false;

                default:
                    return false;
            }
        }

        void HandleTab(bool dedent)
        {
            const CodeSpan selection = Selection();
            const bool multiLine = HasSelection() && selection.begin.line != selection.end.line;
            if (!multiLine && !dedent)
            {
                // Spaces to the next tab stop.
                String spaces;
                const i32 count = TabWidth - (m_cursor.column % TabWidth);
                for (i32 i = 0; i < count; ++i)
                {
                    spaces.PushBack(u8' ');
                }
                InsertText(spaces.AsView(), CodeEditKind::Typing);
                return;
            }

            // Indent/dedent every touched line as ONE undoable replacement.
            const i32 firstLine = selection.begin.line;
            i32 lastLine = selection.end.line;
            if (multiLine && selection.end.column == 0)
            {
                --lastLine; // selection ending at column 0 does not touch that line
            }
            String replacement;
            for (i32 line = firstLine; line <= lastLine; ++line)
            {
                if (line > firstLine)
                {
                    replacement.PushBack(u8'\n');
                }
                const StringView text = m_doc.Line(line);
                if (dedent)
                {
                    usize drop = 0;
                    while (drop < static_cast<usize>(TabWidth) && drop < text.Size() &&
                           text[drop] == u8' ')
                    {
                        ++drop;
                    }
                    replacement.Append(text.SubStr(drop, text.Size() - drop));
                }
                else
                {
                    for (i32 i = 0; i < TabWidth; ++i)
                    {
                        replacement.PushBack(u8' ');
                    }
                    replacement.Append(text);
                }
            }
            const CodeSpan lineSpan{CodePosition{firstLine, 0},
                                    CodePosition{lastLine, m_doc.LineLength(lastLine)}};
            const CodeCursorState before{m_cursor, m_anchor};
            (void)m_doc.Edit(lineSpan, replacement.AsView(), CodeEditKind::Other, before, Now());
            m_anchor = CodePosition{firstLine, 0};
            m_cursor = CodePosition{lastLine, m_doc.LineLength(lastLine)};
            AfterEdit();
        }

        void DuplicateLine()
        {
            const i32 line = m_cursor.line;
            String insert;
            insert.PushBack(u8'\n');
            insert.Append(m_doc.Line(line));
            const CodePosition lineEnd{line, m_doc.LineLength(line)};
            const CodeCursorState before{m_cursor, m_anchor};
            (void)m_doc.Edit(CodeSpan{lineEnd, lineEnd}, insert.AsView(), CodeEditKind::Other,
                             before, Now());
            m_cursor = CodePosition{line + 1, m_cursor.column};
            m_anchor = m_cursor;
            AfterEdit();
        }

        void MoveLine(bool down)
        {
            const i32 line = m_cursor.line;
            const i32 other = down ? line + 1 : line - 1;
            if (other < 0 || other >= m_doc.LineCount())
            {
                return;
            }
            const i32 first = Min(line, other);
            String swapped;
            swapped.Append(m_doc.Line(first + 1));
            swapped.PushBack(u8'\n');
            swapped.Append(m_doc.Line(first));
            const CodeSpan span{CodePosition{first, 0},
                                CodePosition{first + 1, m_doc.LineLength(first + 1)}};
            const CodeCursorState before{m_cursor, m_anchor};
            (void)m_doc.Edit(span, swapped.AsView(), CodeEditKind::Other, before, Now());
            m_cursor = CodePosition{other, m_cursor.column};
            m_anchor = m_cursor;
            AfterEdit();
        }

        void CopySelection(bool cut)
        {
            if (!HasSelection() || Context == nullptr || Context->Clipboard() == nullptr)
            {
                return;
            }
            const String text = SelectedText();
            (void)Context->Clipboard()->SetText(text.AsView());
            if (cut)
            {
                DeleteSpan(Selection(), CodeEditKind::Other);
            }
        }

        void Paste()
        {
            if (Context == nullptr || Context->Clipboard() == nullptr)
            {
                return;
            }
            String text;
            if (!Context->Clipboard()->GetText(text).IsOk() || text.IsEmpty())
            {
                return;
            }
            InsertText(text.AsView(), CodeEditKind::Paste);
        }

        // ---- popup rendering ----

        void DrawCompletionPopup(UIDrawContext& ctx, fonts::CachedFont* font, f32 textLeft,
                                 f32 lineH, f32 advance, f32 ascent, Color background,
                                 Color textColor, Color accent)
        {
            const i32 count = Min(m_completion.ItemCount(), kPopupMaxVisible);
            if (count <= 0)
            {
                return;
            }
            usize maxLabel = 8;
            for (i32 i = 0; i < m_completion.ItemCount(); ++i)
            {
                maxLabel = Max(maxLabel, Utf8Length(m_completion.Item(i)->label.AsView()));
            }
            const f32 popupW =
                Clamp(static_cast<f32>(maxLabel) * advance + 16.0f, 140.0f, 380.0f);
            const f32 popupH = static_cast<f32>(count) * lineH + 6.0f;

            const CodePosition anchor = m_completion.Anchor();
            f32 x = textLeft + static_cast<f32>(anchor.column) * advance - 4.0f;
            f32 y = kPadTop + static_cast<f32>(anchor.line + 1) * lineH - m_scrollY + 2.0f;
            if (y + popupH > Height())
            {
                y = kPadTop + static_cast<f32>(anchor.line) * lineH - m_scrollY - popupH - 2.0f;
            }
            x = Clamp(x, 0.0f, Max(0.0f, Width() - popupW));

            const Color popupBg = Palette::Lighten(background, 0.08f);
            ctx.VG().FillRect(Rectangle{x, y, popupW, popupH}, popupBg);
            ctx.VG().BeginPath();
            ctx.VG().MoveTo(x, y);
            ctx.VG().LineTo(x + popupW, y);
            ctx.VG().LineTo(x + popupW, y + popupH);
            ctx.VG().LineTo(x, y + popupH);
            ctx.VG().LineTo(x, y);
            ctx.VG().Stroke(WithAlpha(textColor, 0.25f), 1.0f);

            // Keep the selected row inside the visible window.
            i32 firstItem = 0;
            if (m_completion.SelectedIndex() >= count)
            {
                firstItem = m_completion.SelectedIndex() - count + 1;
            }
            for (i32 i = 0; i < count; ++i)
            {
                const i32 index = firstItem + i;
                const CompletionCandidate* item = m_completion.Item(index);
                if (item == nullptr)
                {
                    break;
                }
                const f32 rowY = y + 3.0f + static_cast<f32>(i) * lineH;
                if (index == m_completion.SelectedIndex())
                {
                    ctx.VG().FillRect(Rectangle{x + 1.0f, rowY, popupW - 2.0f, lineH},
                                      WithAlpha(accent, 0.3f));
                }
                if (font != nullptr)
                {
                    ctx.VG().DrawText(item->label.AsView(), font,
                                      Float2{x + 8.0f, rowY + ascent}, textColor);
                }
            }
        }

        // ---- state ----

        CodeDocument m_doc;
        CodePosition m_cursor{};
        CodePosition m_anchor{};
        i32 m_desiredColumn = -1; // goal column for vertical motion; -1 = none

        f32 m_scrollX = 0.0f;
        f32 m_scrollY = 0.0f;
        f32 m_viewportW = 0.0f;
        f32 m_viewportH = 0.0f;
        RefPtr<ScrollBar> m_vBar;
        RefPtr<ScrollBar> m_hBar;

        f32 m_lineHeight = 0.0f;
        f32 m_advance = 0.0f;
        i32 m_maxLineLength = 0;
        bool m_maxLineDirty = true;
        bool m_pendingCursorScroll = false;
        bool m_dragging = false;
        f32 m_blinkReset = 0.0f;

        CompletionModel m_completion;
        DocumentWordCompletionProvider m_wordProvider;
        Array<ICompletionProvider*> m_providers; // borrowed
    };

    DRACONIC_DEFINE_OBJECT(CodeEditView, "draconic::ui::toolkit")
}
