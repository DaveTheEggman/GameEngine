// Draconic::EditorScript - the `draconic.editor.script` module.
//
// ScriptEditorPage (scripting.md §5, "ScriptPage - phase 2"): an in-editor text editor for a
// ScriptClassAsset's behavior source. It loads the asset's source file into the shared
// multi-line EditText (undo/redo/selection are the widget's own), and Save writes the source
// back + nudges the SAME recook the external-file edit takes (EditorContext::RequestCook), so a
// running/simulating behavior hot-reloads through the existing ScriptSceneSystem product-swap
// path. Compile errors are surfaced inline: Save (and a debounced type-check) compile-check the
// buffer through the language cook and list the captured ScriptError file:line + message; a
// failing compile keeps the last-good cooked product (the builder never writes on failure).
//
// Backend-neutral: the page never names a language. It resolves the cook + New-Asset starter
// through the registries by the asset's language id, so it edits Wren and AngelScript alike.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module draconic.editor.script;

import draconic.core;
import draconic.content;
import draconic.runtime.client;
import draconic.ui;
import draconic.script;
import draconic.script.editor;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

namespace draconic::editor
{
    void BreakpointGutter::OnMeasure(ui::BoxConstraints constraints)
    {
        MeasuredSize = Float2{24.0f, constraints.ConstrainHeight(0.0f)};
    }

    void BreakpointGutter::OnDraw(ui::UIDrawContext& ctx)
    {
        ctx.VG().FillRect(Rectangle{0, 0, Width(), Height()}, Color{0.12f, 0.13f, 0.16f, 1.0f});
        if (context == nullptr || editor == nullptr)
        {
            return;
        }
        const f32 lineHeight = editor->LineHeight();
        if (lineHeight <= 0.0f)
        {
            return;
        }
        const f32 scrollY = editor->ScrollOffsetY();
        for (const EditorContext::ScriptBreakpoint& breakpoint : context->Breakpoints())
        {
            if (breakpoint.file.AsView() != file.AsView())
            {
                continue;
            }
            const f32 centreY =
                kTopPad + (static_cast<f32>(breakpoint.line) - 0.5f) * lineHeight - scrollY;
            if (centreY < 0.0f || centreY > Height())
            {
                continue;
            }
            ctx.VG().FillCircle(Float2{Width() * 0.5f, centreY}, 4.5f,
                                Color{0.85f, 0.2f, 0.2f, 1.0f});
        }
    }

    void BreakpointGutter::OnMouseDown(ui::MouseEventArgs& e)
    {
        if (e.Button != ui::MouseButton::Left || context == nullptr || editor == nullptr)
        {
            return;
        }
        const f32 lineHeight = editor->LineHeight();
        if (lineHeight <= 0.0f)
        {
            return;
        }
        const i32 line =
            static_cast<i32>((e.Y - kTopPad + editor->ScrollOffsetY()) / lineHeight) + 1;
        if (line >= 1)
        {
            context->ToggleBreakpoint(file.AsView(), line);
            Invalidate();
        }
        e.Handled = true;
    }
    const TypeInfo* ScriptClassPageFactory::PrimaryType() const
    {
        return &draconic::script::ScriptClassAsset::StaticType();
    }

    UniquePtr<EditorPage> ScriptClassPageFactory::CreatePage(EditorContext& context,
                                                             content::Instance& instance)
    {
        auto* page = DefaultAllocator().New<ScriptEditorPage>(context, instance);
        return UniquePtr<EditorPage>(page, DefaultAllocator());
    }
    Status ScriptEditorPage::Save()
    {
        const Status written = m_doc.Save();
        if (written.IsOk())
        {
            ClearDirty();
            // Reuse the external-edit path: the incremental cook rebuilds this asset's
            // product and the app hot-reloads it (ScriptSceneSystem re-instantiates live
            // behaviors + re-applies overrides). A failing cook keeps the last-good product.
            m_context->RequestCook(false);
            // Compile-check now for the inline error surface (the async cook only logs).
            RefreshCompileStatus();
            if (m_doc.LastCompileOk())
            {
                m_context->Notify(NoticeKind::Success, u8"Script saved - recooking");
            }
            else
            {
                m_context->Notify(NoticeKind::Warning,
                                  u8"Script saved with compile errors - last good kept");
            }
        }
        return written;
    }

    void ScriptEditorPage::OnUpdate(draconic::runtime::IApplicationHost&, f32 dt)
    {
        if (m_validateDelay > 0.0f)
        {
            m_validateDelay -= dt;
            if (m_validateDelay <= 0.0f)
            {
                RefreshCompileStatus();
            }
        }
    }

    void ScriptEditorPage::RefreshCompileStatus()
    {
        const bool ok = m_doc.Validate();
        Span<const draconic::script::ScriptSourceDocument::CompileError> errors = m_doc.Errors();
        if (ok)
        {
            String line(u8"Compiled OK");
            if (!m_doc.ClassName().IsEmpty())
            {
                line.Append(u8" - class ");
                line.Append(m_doc.ClassName());
            }
            m_status->SetText(line.AsView());
            m_errorView->SetText(StringView(u8""));
            return;
        }
        String summary;
        AppendCount(summary, errors.Size());
        summary.Append(errors.Size() == 1 ? u8" compile error" : u8" compile errors");
        m_status->SetText(summary.AsView());

        String detail;
        for (const draconic::script::ScriptSourceDocument::CompileError& e : errors)
        {
            if (!detail.IsEmpty())
            {
                detail.PushBack(utf8char('\n'));
            }
            detail.Append(e.module.IsEmpty() ? m_doc.FileName() : e.module.AsView());
            if (e.line > 0)
            {
                detail.PushBack(utf8char(':'));
                AppendCount(detail, static_cast<usize>(e.line));
            }
            detail.Append(u8": ");
            detail.Append(e.message.AsView());
        }
        m_errorView->SetText(detail.AsView());
    }

    void ScriptEditorPage::AppendCount(String& out, usize value)
    {
        utf8char digits[24];
        i32 n = 0;
        usize v = value;
        do
        {
            digits[n++] = static_cast<utf8char>('0' + v % 10);
            v /= 10;
        } while (v > 0 && n < 24);
        while (n > 0)
        {
            out.PushBack(digits[--n]);
        }
    }
}
