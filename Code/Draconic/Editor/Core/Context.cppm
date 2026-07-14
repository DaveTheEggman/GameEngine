// Draconic::EditorCore - :context partition.
//
// EditorContext: the central service object handed to every page/panel/plugin (Sedulous's
// EditorContext, Traktor's IEditor). Holds the open project, the registries, the open pages +
// active page, the global asset selection, and the status sink. Per-subsystem editor modules
// (draconic.<sys>.editor) register their factories here from RegisterEditor(EditorContext&);
// the statically-assembled editor executable calls those entry points (design doc §3.1).

module;
#include "Core/Prelude.h"

export module draconic.editor.core:context;

import draconic.core;
import draconic.resource;
import :importer;
import draconic.content;
import :command;
import :selection;
import :page;
import :project;

using namespace draconic::core;

export namespace draconic::editor
{
    /// User-facing notification severity (the application maps these to UI toasts).
    enum class NoticeKind : u8 { Info, Success, Warning, Error };

    class EditorContext
    {
    public:
        EditorContext() = default;
        EditorContext(const EditorContext&) = delete;
        EditorContext& operator=(const EditorContext&) = delete;

        // === Events ===

        /// Open-pages list or active page changed.
        Function<void()> OnPagesChanged;

        /// Request an incremental cook (wired by the application to its cook service). Pages
        /// call this after saving a builder-backed asset so the cooked product (and every
        /// live proxy bound to it) refreshes without a manual Build > Cook All.
        Function<void(bool /*rebuild*/)> OnCookRequested;
        void RequestCook(bool rebuild = false)
        {
            if (OnCookRequested) { OnCookRequested(rebuild); }
        }
        /// Transient status-bar text.
        Function<void(StringView)> OnStatus;

        /// Transient user-facing notification (toast). Unwired = falls back to the status bar,
        /// so pages can Notify unconditionally.
        Function<void(NoticeKind, StringView)> OnNotice;
        void Notify(NoticeKind kind, StringView message)
        {
            if (OnNotice) { OnNotice(kind, message); }
            else { SetStatus(message); }
        }

        // === Project ===

        /// The app owns the project; the context borrows it (null = no project open).
        void SetProject(EditorProject* project)
        {
            m_project = project;
            m_assetSelection.Clear();
        }
        [[nodiscard]] EditorProject* Project() const noexcept { return m_project; }

        // === Importers (OS file -> Sources/ + typed Asset instance; exe-registered) ===
        [[nodiscard]] ImporterRegistry& Importers() noexcept { return m_importers; }

        // === Resources (runtime products over the project's cooked DB) ===
        // Owned by the application (created at project open); pages resolve scene refs and the
        // inspector's pickers bind through it. Null until a project is open.
        void SetResources(draconic::resource::ResourceManager* resources) noexcept { m_resources = resources; }
        [[nodiscard]] draconic::resource::ResourceManager* Resources() const noexcept { return m_resources; }

        // === Registries ===

        [[nodiscard]] EditorPageRegistry& Pages() noexcept { return m_pageRegistry; }

        /// Asset creators (File > New <label>): create a fresh source instance in the project DB.
        /// Registered by per-subsystem editor modules; the shell builds menu items from them.
        struct AssetCreator
        {
            String label;
            // Menu grouping: creators sharing a category land in a submenu of that name
            // ("Primitives"); empty = a top-level "New <label>" item.
            String category;
            // `group` = the browser group the user invoked the creator FROM (null = no context,
            // e.g. the File menu - the creator picks its own default group).
            Function<draconic::content::Instance*(EditorContext&, draconic::content::Group*)> create;
            // Only document-like creations (scenes) become the project's default scene when it
            // is unset; data assets (primitive meshes, materials) never should.
            bool setsDefaultScene = false;
        };

        // === Favorites (pinned asset instances - the browser + picker surface them first) ===

        [[nodiscard]] bool IsFavorite(const Guid& id) const
        {
            for (const Guid& f : m_favorites) { if (f == id) { return true; } }
            return false;
        }
        void ToggleFavorite(const Guid& id)
        {
            for (usize i = 0; i < m_favorites.Size(); ++i)
            {
                if (m_favorites[i] == id)
                {
                    m_favorites.RemoveAt(i);
                    if (OnFavoritesChanged) { OnFavoritesChanged(); }
                    return;
                }
            }
            m_favorites.PushBack(id);
            if (OnFavoritesChanged) { OnFavoritesChanged(); }
        }
        [[nodiscard]] Span<const Guid> Favorites() const noexcept
        {
            return Span<const Guid>{ m_favorites.Data(), m_favorites.Size() };
        }
        void SetFavorites(Array<Guid> favorites) { m_favorites = Move(favorites); }
        /// Fired on every toggle (the app persists to the project's Editor/ state).
        Function<void()> OnFavoritesChanged;

        // === Editor clipboard (cross-page: entity subtrees, components) ===
        // One typed slot: `kind` says what the blob is ("entities", "component"); consumers
        // check the kind before parsing. Cleared by overwrite only.

        void SetClipboard(StringView kind, Array<byte> data)
        {
            m_clipboardKind = String(kind);
            m_clipboard = Move(data);
        }
        [[nodiscard]] StringView ClipboardKind() const noexcept { return m_clipboardKind.AsView(); }
        [[nodiscard]] Span<const byte> ClipboardData(StringView kind) const noexcept
        {
            return (m_clipboardKind == kind)
                ? Span<const byte>{ m_clipboard.Data(), m_clipboard.Size() } : Span<const byte>{};
        }

        void RegisterCreator(AssetCreator creator)
        {
            if (creator.create) { m_creators.PushBack(Move(creator)); }
        }

        [[nodiscard]] Span<const AssetCreator> Creators() const noexcept
        {
            return Span<const AssetCreator>{ m_creators.Data(), m_creators.Size() };
        }

        // === Open pages ===

        /// Open (or focus) a page editing `instance`: an existing page for the same instance is
        /// activated; otherwise the registry's nearest-type factory creates one. Null if no
        /// factory matches or the instance's type isn't registered.
        EditorPage* OpenPage(draconic::content::Instance& instance)
        {
            for (const UniquePtr<EditorPage>& page : m_pages)
            {
                if (page->InstanceId() == instance.Id())
                {
                    SetActivePage(page.Get());
                    return page.Get();
                }
            }

            const TypeInfo* type = GlobalTypeRegistry().FindByName(
                reinterpret_cast<const char*>(String(instance.TypeNamespace()).CStr()),
                reinterpret_cast<const char*>(String(instance.TypeName()).CStr()));
            if (type == nullptr) { return nullptr; }

            IEditorPageFactory* factory = m_pageRegistry.FindFactory(*type);
            if (factory == nullptr) { return nullptr; }

            UniquePtr<EditorPage> page = factory->CreatePage(*this, instance);
            if (!page) { return nullptr; }
            page->SetInstanceId(instance.Id());

            EditorPage* raw = page.Get();
            m_pages.PushBack(Move(page));
            m_activePage = raw;
            NotifyPagesChanged();
            return raw;
        }

        /// Close a page (the caller is responsible for save-prompting dirty pages first).
        void ClosePage(EditorPage* page)
        {
            for (usize i = 0; i < m_pages.Size(); ++i)
            {
                if (m_pages[i].Get() == page)
                {
                    if (m_activePage == page)
                    {
                        m_activePage = m_pages.Size() > 1
                            ? m_pages[i + 1 < m_pages.Size() ? i + 1 : i - 1].Get()
                            : nullptr;
                    }
                    m_pages.RemoveAt(i);
                    NotifyPagesChanged();
                    return;
                }
            }
        }

        [[nodiscard]] Span<const UniquePtr<EditorPage>> OpenPages() const noexcept
        {
            return Span<const UniquePtr<EditorPage>>{ m_pages.Data(), m_pages.Size() };
        }

        [[nodiscard]] EditorPage* ActivePage() const noexcept { return m_activePage; }
        void SetActivePage(EditorPage* page)
        {
            if (m_activePage == page) { return; }
            m_activePage = page;
            NotifyPagesChanged();
        }

        // === Edit routing (menu Edit>Undo/Redo -> the active page's stack) ===

        [[nodiscard]] bool CanUndo() const { return m_activePage != nullptr && m_activePage->Commands().CanUndo(); }
        [[nodiscard]] bool CanRedo() const { return m_activePage != nullptr && m_activePage->Commands().CanRedo(); }
        void Undo() { if (m_activePage != nullptr) { m_activePage->Commands().Undo(); } }
        void Redo() { if (m_activePage != nullptr) { m_activePage->Commands().Redo(); } }

        // === Selection ===

        /// Global asset selection (asset browser / instance pickers). Entity selection is
        /// per-scene-page (phase 3).
        [[nodiscard]] Selection<const draconic::content::Instance*>& AssetSelection() noexcept
        {
            return m_assetSelection;
        }

        // === Status ===

        void SetStatus(StringView text)
        {
            if (OnStatus) { OnStatus(text); }
        }

    private:
        void NotifyPagesChanged()
        {
            if (OnPagesChanged) { OnPagesChanged(); }
        }

        EditorProject* m_project = nullptr;
        draconic::resource::ResourceManager* m_resources = nullptr;   // borrowed (app-owned)
        ImporterRegistry m_importers;   // borrowed
        EditorPageRegistry m_pageRegistry;
        Array<AssetCreator> m_creators;
        String m_clipboardKind;
        Array<byte> m_clipboard;
        Array<Guid> m_favorites;
        Array<UniquePtr<EditorPage>> m_pages;
        EditorPage* m_activePage = nullptr;
        Selection<const draconic::content::Instance*> m_assetSelection;
    };
}
