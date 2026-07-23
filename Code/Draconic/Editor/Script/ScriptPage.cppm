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

export module draconic.editor.script;

import draconic.core;
import draconic.content;
import draconic.runtime.client;
import draconic.ui;
import draconic.script;
import draconic.script.editor;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace ui = draconic::ui;
    namespace content = draconic::content;

    // A clickable breakpoint gutter beside the source editor (script-debugger.md P1): click a
    // line to toggle a breakpoint in the shared EditorContext store; a red dot marks each
    // breakpoint line, aligned to the editor's line height + scroll. Contract-neutral - it
    // only edits the store; a Game run applies the same set to its debugger.
    class BreakpointGutter final : public ui::View
    {
    public:
        EditorContext* context = nullptr; // the shared breakpoint store (borrowed)
        ui::EditText* editor = nullptr;   // line-metric source (borrowed)
        String file;                      // the source file these breakpoints key on

        void OnMeasure(ui::BoxConstraints constraints) override
        {
            MeasuredSize = Float2{24.0f, constraints.ConstrainHeight(0.0f)};
        }

        void OnDraw(ui::UIDrawContext& ctx) override
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

        void OnMouseDown(ui::MouseEventArgs& e) override
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

    private:
        static constexpr f32 kTopPad = 4.0f; // the editor's top text padding (Thickness{6,4})
    };

    // The in-editor script text page. Pure UI over ScriptSourceDocument (the headless save +
    // compile-check model): the page owns the widget tree and forwards edits/Save to the model.
    class ScriptEditorPage final : public app::UIEditorPage
    {
    public:
        ScriptEditorPage(EditorContext& context, content::Instance& instance)
            : m_context(&context), m_title(instance.Name())
        {
            SetInstanceId(instance.Id());

            // The asset carries only the file name + language; the source lives in Sources/.
            String sourcesRoot;
            if (m_context->Project() != nullptr)
            {
                sourcesRoot = m_context->Project()->SourcesRoot();
            }
            RefPtr<ISerializable> object = instance.ReadObject();
            if (auto* asset = Cast<draconic::script::ScriptClassAsset>(object.Get()))
            {
                m_doc.Bind(sourcesRoot.AsView(), asset->fileName.AsView(),
                           asset->language.AsView());
            }
            (void)m_doc.Load(); // an unreadable file just leaves an empty buffer

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 4.0f;

            // The source editor: the shared multi-line EditText (its own undo/redo/selection).
            m_editor = MakeRef<ui::EditText>(DefaultAllocator());
            m_editor->Multiline.SetValue(true);
            m_editor->SetText(m_doc.Source());
            ScriptEditorPage* self = this;
            m_editor->OnTextChanged.Add(
                [self](ui::EditText* edit)
                {
                    self->m_doc.SetSource(edit->Text());
                    self->MarkDirty();
                    self->m_validateDelay = 0.6f; // debounce a background compile-check
                });

            // A horizontal row: [breakpoint gutter | source editor]. The gutter toggles
            // breakpoints in the shared store; a Game run applies them to its debugger.
            auto editorRow = MakeRef<ui::FlexLayout>(DefaultAllocator());
            editorRow->Direction = ui::Orientation::Horizontal;
            m_gutter = MakeRef<BreakpointGutter>(DefaultAllocator());
            m_gutter->context = &context;
            m_gutter->editor = m_editor.Get();
            m_gutter->file = String(m_doc.FileName());
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(24));
                lp->Height = ui::SizeSpec::Match();
                editorRow->AddView(m_gutter.Get(), lp);
            }
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                lp->Height = ui::SizeSpec::Match();
                editorRow->AddView(m_editor.Get(), lp);
            }
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                lp->Width = ui::SizeSpec::Match();
                column->AddView(editorRow.Get(), lp);
            }

            // A one-line compile status (OK / N error(s) / no cook).
            m_status = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                column->AddView(m_status.Get(), lp);
            }

            // The compile-error list: a read-only multi-line view (file:line + message).
            m_errorView = MakeRef<ui::EditText>(DefaultAllocator());
            m_errorView->Multiline.SetValue(true);
            m_errorView->IsReadOnly.SetValue(true);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 0.35f;
                lp->Width = ui::SizeSpec::Match();
                column->AddView(m_errorView.Get(), lp);
            }

            m_content = column;
            RefreshCompileStatus(); // initial pass so the page opens with live state
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }

        [[nodiscard]] Status Save() override
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

        void OnUpdate(draconic::runtime::IApplicationHost&, f32 dt) override
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

    private:
        // Compile-check the current buffer and repaint the status line + error list. Never
        // touches the edit views' identity (only SetText on the read-only surfaces), so it is
        // safe to call from an event dispatch without the UI mutation queue.
        void RefreshCompileStatus()
        {
            const bool ok = m_doc.Validate();
            Span<const draconic::script::ScriptSourceDocument::CompileError> errors =
                m_doc.Errors();
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

        static void AppendCount(String& out, usize value)
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

        EditorContext* m_context = nullptr;
        draconic::script::ScriptSourceDocument m_doc;
        String m_title;
        f32 m_validateDelay = 0.0f;
        RefPtr<ui::View> m_content;
        RefPtr<ui::EditText> m_editor;
        RefPtr<BreakpointGutter> m_gutter;
        RefPtr<ui::Label> m_status;
        RefPtr<ui::EditText> m_errorView;
    };

    class ScriptClassPageFactory final : public IEditorPageFactory
    {
    public:
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &draconic::script::ScriptClassAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       content::Instance& instance) override
        {
            auto* page = DefaultAllocator().New<ScriptEditorPage>(context, instance);
            return UniquePtr<EditorPage>(page, DefaultAllocator());
        }
    };

    // Seeds a fresh script asset: a starter source file (the language cook's NewAssetTemplate -
    // never hardcoded text) copied into Sources/, plus a ScriptClassAsset recording its file +
    // language. Backend-neutral: the caller passes the language id + extension from the backend
    // registry, so this works for whichever language the New Asset item is for.
    inline content::Instance* CreateScriptInstance(EditorContext& context, content::Group* group,
                                                   StringView languageId, StringView extension)
    {
        if (context.Project() == nullptr)
        {
            return nullptr;
        }
        content::Group* target =
            group != nullptr ? group : context.Project()->SourceDb().RootGroup();

        String name(u8"NewBehavior");
        for (i32 counter = 2; target->GetInstance(name.AsView()) != nullptr; ++counter)
        {
            name = String(u8"NewBehavior");
            if (counter >= 10)
            {
                name.PushBack(static_cast<utf8char>('0' + (counter / 10 % 10)));
            }
            name.PushBack(static_cast<utf8char>('0' + (counter % 10)));
        }

        String fileName(name.AsView());
        fileName.PushBack(utf8char('.'));
        fileName.Append(extension);

        draconic::script::IScriptLanguageCook* cook =
            draconic::script::ScriptLanguageCookRegistry::Get().FindByLanguage(languageId);
        if (cook == nullptr)
        {
            return nullptr;
        }
        const StringView starter = cook->NewAssetTemplate();

        const String path = PathJoin(context.Project()->SourcesRoot().AsView(), fileName.AsView());
        if (!WriteFile(
                 path.AsView(),
                 Span<const byte>(reinterpret_cast<const byte*>(starter.Data()), starter.Size()))
                 .IsOk())
        {
            return nullptr;
        }

        content::Instance* instance =
            target->CreateInstance(name.AsView(), draconic::script::ScriptClassAsset::StaticType());
        if (instance == nullptr)
        {
            return nullptr;
        }
        draconic::script::ScriptClassAsset asset;
        asset.fileName = fileName;
        asset.language = String(languageId);
        if (!instance->WriteObject(asset).IsOk())
        {
            return nullptr;
        }
        context.RequestCook(false); // cook now so the new class is pickable + attachable
        return instance;
    }

    /// The editor executable's entry point for the script plugin: registers the ScriptPage
    /// factory + one New-Asset creator per registered script backend (Wren, AngelScript, ...).
    inline void RegisterScriptEditor(EditorContext& context)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<ScriptClassPageFactory>(), DefaultAllocator()));

        const auto backends = draconic::script::ScriptBackendRegistry::Get().All();
        DRACONIC_LOG_INFO(u8"Editor",
                          u8"RegisterScriptEditor: {} script backend(s) in the registry",
                          backends.Size());
        core::u32 registeredCreators = 0;
        for (const draconic::script::ScriptBackendDesc& backend : backends)
        {
            // A backend with no cook (compile/harvest) cannot seed a starter - skip it.
            if (draconic::script::ScriptLanguageCookRegistry::Get().FindByLanguage(
                    backend.languageId.AsView()) == nullptr)
            {
                DRACONIC_LOG_WARNING(
                    u8"Editor",
                    u8"  script backend '{}' has NO registered cook - no New-Asset creator",
                    backend.languageId);
                continue;
            }
            String extension = backend.fileExtensions.IsEmpty()
                                   ? String(backend.languageId.AsView())
                                   : String(backend.fileExtensions[0].AsView());
            String label(backend.displayName.IsEmpty() ? backend.languageId.AsView()
                                                       : backend.displayName.AsView());
            label.Append(u8" Script");

            EditorContext::AssetCreator creator;
            creator.label = Move(label);
            creator.category = String(u8"Scripts");
            String languageId(backend.languageId.AsView());
            creator.create = [languageId = Move(languageId),
                              extension = Move(extension)](EditorContext& ctx, content::Group* g)
            { return CreateScriptInstance(ctx, g, languageId.AsView(), extension.AsView()); };
            context.RegisterCreator(Move(creator));
            ++registeredCreators;
        }
        DRACONIC_LOG_INFO(u8"Editor",
                          u8"RegisterScriptEditor: {} script New-Asset creator(s) registered",
                          registeredCreators);
    }
}
