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

        void OnMeasure(ui::BoxConstraints constraints) override;

        void OnDraw(ui::UIDrawContext& ctx) override;

        void OnMouseDown(ui::MouseEventArgs& e) override;

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

        [[nodiscard]] Status Save() override;

        void OnUpdate(draconic::runtime::IApplicationHost&, f32 dt) override;

    private:
        // Compile-check the current buffer and repaint the status line + error list. Never
        // touches the edit views' identity (only SetText on the read-only surfaces), so it is
        // safe to call from an event dispatch without the UI mutation queue.
        void RefreshCompileStatus();

        static void AppendCount(String& out, usize value);

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
        [[nodiscard]] const TypeInfo* PrimaryType() const override;
        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       content::Instance& instance) override;
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
