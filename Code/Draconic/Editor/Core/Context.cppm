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
    enum class NoticeKind : u8
    {
        Info,
        Success,
        Warning,
        Error
    };

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
            if (OnCookRequested)
            {
                OnCookRequested(rebuild);
            }
        }
        /// Transient status-bar text.
        Function<void(StringView)> OnStatus;

        /// Transient user-facing notification (toast). Unwired = falls back to the status bar,
        /// so pages can Notify unconditionally.
        Function<void(NoticeKind, StringView)> OnNotice;
        void Notify(NoticeKind kind, StringView message)
        {
            if (OnNotice)
            {
                OnNotice(kind, message);
            }
            else
            {
                SetStatus(message);
            }
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
        void SetResources(draconic::resource::ResourceManager* resources) noexcept
        {
            m_resources = resources;
        }
        [[nodiscard]] draconic::resource::ResourceManager* Resources() const noexcept
        {
            return m_resources;
        }

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
            Function<draconic::content::Instance*(EditorContext&, draconic::content::Group*)>
                create;
            // Only document-like creations (scenes) become the project's default scene when it
            // is unset; data assets (primitive meshes, materials) never should.
            bool setsDefaultScene = false;
        };

        // === Favorites (pinned asset instances - the browser + picker surface them first) ===

        [[nodiscard]] bool IsFavorite(const Guid& id) const
        {
            for (const Guid& f : m_favorites)
            {
                if (f == id)
                {
                    return true;
                }
            }
            return false;
        }
        void ToggleFavorite(const Guid& id)
        {
            for (usize i = 0; i < m_favorites.Size(); ++i)
            {
                if (m_favorites[i] == id)
                {
                    m_favorites.RemoveAt(i);
                    if (OnFavoritesChanged)
                    {
                        OnFavoritesChanged();
                    }
                    return;
                }
            }
            m_favorites.PushBack(id);
            if (OnFavoritesChanged)
            {
                OnFavoritesChanged();
            }
        }
        [[nodiscard]] Span<const Guid> Favorites() const noexcept
        {
            return Span<const Guid>{m_favorites.Data(), m_favorites.Size()};
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
                       ? Span<const byte>{m_clipboard.Data(), m_clipboard.Size()}
                       : Span<const byte>{};
        }

        void RegisterCreator(AssetCreator creator)
        {
            if (creator.create)
            {
                m_creators.PushBack(Move(creator));
            }
        }

        [[nodiscard]] Span<const AssetCreator> Creators() const noexcept
        {
            return Span<const AssetCreator>{m_creators.Data(), m_creators.Size()};
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
            if (type == nullptr)
            {
                return nullptr;
            }

            IEditorPageFactory* factory = m_pageRegistry.FindFactory(*type);
            if (factory == nullptr)
            {
                return nullptr;
            }

            UniquePtr<EditorPage> page = factory->CreatePage(*this, instance);
            if (!page)
            {
                return nullptr;
            }
            page->SetInstanceId(instance.Id());

            EditorPage* raw = page.Get();
            m_pages.PushBack(Move(page));
            m_activePage = raw;
            NotifyPagesChanged();
            return raw;
        }

        /// Adopt an instance-LESS page (the Game tab): same ownership + active-page flow as
        /// OpenPage, but the caller constructs it (no instance, no factory dispatch).
        EditorPage* AdoptPage(UniquePtr<EditorPage> page)
        {
            if (!page)
            {
                return nullptr;
            }
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
            return Span<const UniquePtr<EditorPage>>{m_pages.Data(), m_pages.Size()};
        }

        [[nodiscard]] EditorPage* ActivePage() const noexcept { return m_activePage; }
        void SetActivePage(EditorPage* page)
        {
            if (m_activePage == page)
            {
                return;
            }
            m_activePage = page;
            NotifyPagesChanged();
        }

        // === Edit routing (menu Edit>Undo/Redo -> the active page's stack) ===

        [[nodiscard]] bool CanUndo() const
        {
            return m_activePage != nullptr && m_activePage->Commands().CanUndo();
        }
        [[nodiscard]] bool CanRedo() const
        {
            return m_activePage != nullptr && m_activePage->Commands().CanRedo();
        }
        void Undo()
        {
            if (m_activePage != nullptr)
            {
                m_activePage->Commands().Undo();
            }
        }
        void Redo()
        {
            if (m_activePage != nullptr)
            {
                m_activePage->Commands().Redo();
            }
        }

        // === Selection ===

        /// Global asset selection (asset browser / instance pickers). Entity selection is
        /// per-scene-page (phase 3).
        [[nodiscard]] Selection<const draconic::content::Instance*>& AssetSelection() noexcept
        {
            return m_assetSelection;
        }

        // === Import notifications ===

        /// Subscribe to successful file imports (fired by the import flow AFTER the importer
        /// returned; `options` is the dialog-edited options object, null when none). Used for
        /// post-import steps that live above the importer's layer - e.g. model->prefab
        /// generation, which needs scene machinery the importer library never links.
        void AddImportListener(
            Function<void(draconic::content::Instance&, const ImportOptions*)> listener)
        {
            m_importListeners.PushBack(Move(listener));
        }

        /// Play-in-editor seam: creates the singleton Game page (the player behavior in a
        /// tab). Registered by the scene editor plugin; unset = the Game menu item notifies.
        // Creates a Game tab. `newInstance` = false reuses the app's primary GameInstance (the normal
        // Play); true spins up an ADDITIONAL instance (multi-instance PIE, game-instance.md §11 step 5).
        Function<UniquePtr<EditorPage>(bool newInstance)> GamePageFactory;
        /// Stops the Game tab's live run, if any (the embedded app's RequestExit lands
        /// here, deferred to after the page-update loop). Set by the Game page.
        Function<void()> StopGameRun;

        /// Export seam: transcodes a scene/prefab instance's TEXT source stream to the
        /// binary wire for staging. Registered by the scene editor plugin (needs scene
        /// machinery editor.core never links); returns false for non-scene instances or
        /// on failure (the exporter then stages the source verbatim - the runtime sniffs).
        /// MAIN-THREAD only (creates a scratch scene through the SceneSubsystem).
        Function<bool(draconic::content::Instance&, Array<byte>&)> SceneStreamStager;

        /// Export reachability seam: collects the assets a scene/prefab instance references, for the
        /// export closure (docs/design/export-reachability.md). Loads the instance over the app's full
        /// manager set, appending its component resource Ref ids to `outResources` and its nested
        /// prefab-instance ids to `outPrefabs`; returns false for non-scene instances or on failure.
        /// Registered by the scene editor plugin (needs scene machinery). MAIN-THREAD only (loads a
        /// scene through the SceneSubsystem), which is exactly why the editor pre-scans with this on
        /// the main thread and hands the resulting guid set to the background export job.
        Function<bool(draconic::content::Instance&, draconic::content::ContentDatabase&,
                      Array<Guid>& /*outResources*/, Array<Guid>& /*outPrefabs*/)>
            SceneRefScanner;

        void NotifyImported(draconic::content::Instance& instance, const ImportOptions* options)
        {
            for (const auto& listener : m_importListeners)
            {
                listener(instance, options);
            }
        }

        // === Script breakpoints (the debugger, script-debugger.md P1) ===
        // Shared editor state: the ScriptPage gutter toggles them per source file+line, and a
        // Game run applies them to its script debugger. Contract-neutral plain data (a future
        // remote debugger consumes the same set).

        struct ScriptBreakpoint
        {
            String file;  // source file name (the section the runtime reports)
            i32 line = 0; // 1-based
        };

        /// Toggle a breakpoint at `file:line`; fires OnBreakpointsChanged.
        void ToggleBreakpoint(StringView file, i32 line)
        {
            for (usize i = 0; i < m_breakpoints.Size(); ++i)
            {
                if (m_breakpoints[i].line == line && m_breakpoints[i].file.AsView() == file)
                {
                    m_breakpoints.RemoveAt(i);
                    if (OnBreakpointsChanged)
                    {
                        OnBreakpointsChanged();
                    }
                    return;
                }
            }
            m_breakpoints.PushBack(ScriptBreakpoint{String(file), line});
            if (OnBreakpointsChanged)
            {
                OnBreakpointsChanged();
            }
        }
        [[nodiscard]] bool HasBreakpoint(StringView file, i32 line) const
        {
            for (const ScriptBreakpoint& breakpoint : m_breakpoints)
            {
                if (breakpoint.line == line && breakpoint.file.AsView() == file)
                {
                    return true;
                }
            }
            return false;
        }
        [[nodiscard]] Span<const ScriptBreakpoint> Breakpoints() const noexcept
        {
            return Span<const ScriptBreakpoint>{m_breakpoints.Data(), m_breakpoints.Size()};
        }
        /// Fired on every breakpoint toggle (the gutter repaints; a live run re-applies).
        Function<void()> OnBreakpointsChanged;

        // === Status ===

        void SetStatus(StringView text)
        {
            if (OnStatus)
            {
                OnStatus(text);
            }
        }

    private:
        void NotifyPagesChanged()
        {
            if (OnPagesChanged)
            {
                OnPagesChanged();
            }
        }

        EditorProject* m_project = nullptr;
        draconic::resource::ResourceManager* m_resources = nullptr; // borrowed (app-owned)
        ImporterRegistry m_importers;                               // borrowed
        EditorPageRegistry m_pageRegistry;
        Array<AssetCreator> m_creators;
        String m_clipboardKind;
        Array<byte> m_clipboard;
        Array<Guid> m_favorites;
        Array<Function<void(draconic::content::Instance&, const ImportOptions*)>> m_importListeners;
        Array<UniquePtr<EditorPage>> m_pages;
        EditorPage* m_activePage = nullptr;
        Selection<const draconic::content::Instance*> m_assetSelection;
        Array<ScriptBreakpoint> m_breakpoints; // shared script debugger breakpoints
    };
}
