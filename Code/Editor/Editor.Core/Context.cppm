// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Core - :context partition.
//
// EditorContext: the central service object handed to every page/panel/plugin (Sedulous's
// EditorContext, Traktor's IEditor). Holds the open project, the registries, the open pages +
// active page, the global asset selection, and the status sink. Per-subsystem editor modules
// register their factories here from RegisterEditor(EditorContext&);
// the statically-assembled editor executable calls those entry points.

module;
#include "Core/Prelude.h"

export module editor.core:context;

import foundation.core;
import foundation.resource;
import foundation.settings;
import pipeline.importer;
import foundation.content;
import :command;
import :selection;
import :page;
import :job_service;
import :thumbnail_service;
import :project;

using namespace foundation::core;

export namespace editor
{
    namespace detail
    {
        inline IAllocator*& EditorRootSlot() noexcept
        {
            static IAllocator* slot = &DefaultAllocator();
            return slot;
        }
    }

    /// The EDITOR BINARY's root allocator seam: the editor app installs its tagged
    /// "Editor" root at startup, so free helpers and panel providers (which have no
    /// owner parameter to thread) still attribute to the Editor tag. Outside the app
    /// (unit tests) it is the process allocator - tests are their own roots.
    inline void SetEditorRootAllocator(IAllocator& allocator) noexcept
    {
        detail::EditorRootSlot() = &allocator;
    }
    [[nodiscard]] inline IAllocator& EditorRootAllocator() noexcept
    {
        return *detail::EditorRootSlot();
    }

    /// User-facing notification severity (the application maps these to UI toasts).
    enum class NoticeKind : u8
    {
        Info,
        Success,
        Warning,
        Error
    };

    /// A sink for live-asset-edit persistence closures (terrain sculpt and future brushes).
    /// EditorContext implements it; the viewport-tool framework (editor.viewporttools) holds a
    /// BORROWED IAssetEditSink* so a domain tool can register a write-back-to-source closure
    /// WITHOUT the framework depending on EditorContext. The sink outlives every page/tool.
    class IAssetEditSink
    {
    public:
        virtual ~IAssetEditSink() = default;
        virtual void
        RegisterAssetEdit(const Guid& assetId,
                          Function<Status(foundation::content::ContentDatabase&)> persist) = 0;
    };

    class EditorContext : public IAssetEditSink
    {
    public:
        // The allocator (required - the editor app passes its tagged "Editor" root)
        // is the authority every page, panel, and editor service builds on.
        explicit EditorContext(IAllocator& allocator) noexcept : m_allocator(&allocator) {}

        [[nodiscard]] IAllocator& Allocator() const noexcept { return *m_allocator; }
        EditorContext(const EditorContext&) = delete;
        EditorContext& operator=(const EditorContext&) = delete;

        // === Events ===

        /// Open-pages list or active page changed.
        Function<void()> OnPagesChanged;
        /// Fired after project settings are SAVED (the settings dialog invokes it via
        /// NotifyProjectSettingsChanged) so session state derived from settings - the
        /// default UI font/theme binds - re-applies without a project reopen.
        Function<void()> OnProjectSettingsChanged;
        void NotifyProjectSettingsChanged()
        {
            if (OnProjectSettingsChanged)
            {
                OnProjectSettingsChanged();
            }
        }

        /// Request an incremental cook (wired by the application to its cook service). Pages
        /// call this after saving a builder-backed asset so the cooked product (and every
        /// live proxy bound to it) refreshes without a manual Build > Cook All.
        Function<void(bool /*rebuild*/)> OnCookRequested;
        void RequestCook(bool rebuild = false);
        /// Cook-activity query (wired by the application to EditorCookService::IsIdle's
        /// negation). Pages that must NOT start against a half-written cooked DB (PIE)
        /// poll this and defer. Unwired (tests, no project) = never busy.
        Function<bool()> CookBusy;
        [[nodiscard]] bool IsCookBusy() const { return CookBusy ? CookBusy() : false; }

        // === Pending asset edits (scene-viewport tools that edit an asset LIVE, e.g. terrain sculpt) ===
        // A viewport tool edits a cooked product live (immediate feedback) and registers a closure
        // that persists the edit back to its SOURCE asset. The editor save flow DRAINS this, passing
        // the source ContentDatabase it owns + requesting a recook on success. Last edit per asset
        // guid wins. This keeps WHAT is dirty + HOW to persist it without the framework knowing the
        // domain - the closure lives in the domain editor lib (Editor.Terrain), and it is handed the
        // DB at drain time so it captures no DB handle.
        void RegisterAssetEdit(const Guid& assetId,
                               Function<Status(foundation::content::ContentDatabase&)> persist) override;
        [[nodiscard]] bool HasPendingAssetEdits() const noexcept
        {
            return !m_pendingAssetEdits.IsEmpty();
        }
        /// Run + clear every pending edit, persisting through `db`; requests a recook if any succeeded.
        Status DrainAssetEdits(foundation::content::ContentDatabase& db);

        /// Transient status-bar text.
        Function<void(StringView)> OnStatus;

        /// Transient user-facing notification (toast). Unwired = falls back to the status bar,
        /// so pages can Notify unconditionally.
        Function<void(NoticeKind, StringView)> OnNotice;

        /// Open the asset with this Guid for editing: the app
        /// maps Guid -> content Instance -> the SAME page/panel path a browser double-click
        /// takes. Null-tolerant (callers guard).
        Function<void(const Guid&)> OpenAsset;

        /// Asset-open INTERCEPTION: a claimant (e.g.
        /// the scene page, for animation clips into its animation bar) sees an OpenAsset
        /// BEFORE it routes to a page; returning true claims it. Consulted newest-first, so
        /// the most recently opened claimant wins; claimants gate themselves on visibility.
        /// Remove with the returned id when the claimant dies (pages unregister on close).
        using OpenAssetInterceptor = Function<bool(foundation::content::Instance&)>;
        u64 AddOpenAssetInterceptor(OpenAssetInterceptor interceptor)
        {
            const u64 id = ++m_nextInterceptorId;
            m_openInterceptors.PushBack(InterceptorEntry{id, Move(interceptor)});
            return id;
        }
        void RemoveOpenAssetInterceptor(u64 id)
        {
            for (usize i = 0; i < m_openInterceptors.Size(); ++i)
            {
                if (m_openInterceptors[i].id == id)
                {
                    m_openInterceptors.RemoveAt(i);
                    return;
                }
            }
        }
        [[nodiscard]] bool TryInterceptOpenAsset(foundation::content::Instance& instance)
        {
            for (usize i = m_openInterceptors.Size(); i > 0; --i) // newest-first
            {
                if (m_openInterceptors[i - 1].fn && m_openInterceptors[i - 1].fn(instance))
                {
                    return true;
                }
            }
            return false;
        }
        /// Reveal the asset with this Guid in the asset browser (select + scroll into view).
        Function<void(const Guid&)> RevealAsset;
        void Notify(NoticeKind kind, StringView message);

    private:
        IAllocator* m_allocator;
        struct InterceptorEntry
        {
            u64 id = 0;
            OpenAssetInterceptor fn;
        };
        Array<InterceptorEntry> m_openInterceptors;
        u64 m_nextInterceptorId = 0;

        struct PendingAssetEdit
        {
            Guid id;
            Function<Status(foundation::content::ContentDatabase&)> persist;
        };
        Array<PendingAssetEdit> m_pendingAssetEdits;

    public:

        // === Project ===

        /// The app owns the project; the context borrows it (null = no project open).
        void SetProject(EditorProject* project);

        // The app's background-job runner (build lane + light lane). May be null in
        // headless/test contexts - callers must fall back to synchronous work.
        [[nodiscard]] EditorJobService* Jobs() const noexcept { return m_jobs; }
        void SetJobs(EditorJobService* jobs) noexcept { m_jobs = jobs; }
        [[nodiscard]] ThumbnailService* Thumbnails() const noexcept { return m_thumbnails; }
        void SetThumbnails(ThumbnailService* thumbnails) noexcept { m_thumbnails = thumbnails; }
        [[nodiscard]] EditorProject* Project() const noexcept { return m_project; }

        // === Importers (OS file -> Sources/ + typed Asset instance; exe-registered) ===
        [[nodiscard]] pipeline::ImporterRegistry& Importers() noexcept { return m_importers; }

        // === Resources (runtime products over the project's cooked DB) ===
        // Owned by the application (created at project open); pages resolve scene refs and the
        // inspector's pickers bind through it. Null until a project is open.
        void SetResources(foundation::resource::ResourceManager* resources) noexcept;

        /// The PER-PROJECT editor-state settings store (<project>/Editor/ - dock layout,
        /// favorites, open pages, per-page prefs). Borrowed; the app owns it and sets it for
        /// the lifetime of the open project (null between projects). Pages mutate their
        /// section + MarkChanged, then RequestProjectEditorSettingsSave() to persist.
        void SetProjectEditorSettings(foundation::settings::Settings* store) noexcept
        {
            m_projectEditorSettings = store;
        }
        [[nodiscard]] foundation::settings::Settings* ProjectEditorSettings() const noexcept
        {
            return m_projectEditorSettings;
        }
        Function<void()> OnProjectEditorSettingsSaveRequested; // app-bound persist hook
        void RequestProjectEditorSettingsSave()
        {
            if (OnProjectEditorSettingsSaveRequested)
            {
                OnProjectEditorSettingsSaveRequested();
            }
        }

        /// The PER-USER editor settings store (<user-data>/editor.settings.xml). Borrowed;
        /// the app owns it and sets it at boot. Domains keep their OWN typed sections in it
        /// (Section<T>() + MarkChanged) - the app never learns their shapes.
        void SetUserEditorSettings(foundation::settings::Settings* store) noexcept
        {
            m_userEditorSettings = store;
        }
        [[nodiscard]] foundation::settings::Settings* UserEditorSettings() const noexcept
        {
            return m_userEditorSettings;
        }

        // --- domain-contributed editor settings -------------------------------------------
        // A DOMAIN (navigation, terrain, ...) registers a category of preference fields; the
        // Preferences dialog renders every contribution generically - the app hardcodes
        // nothing. Fields carry get/set closures, so where the value lives (usually the
        // domain's own section in UserEditorSettings) stays the domain's business. v1 =
        // bool fields (grow kinds as domains need them).
        struct EditorSettingsBoolField
        {
            String label;
            String description; // tooltip (empty = none)
            Function<bool()> get;
            Function<void(bool)> set;
        };
        struct EditorSettingsContribution
        {
            String category; // dialog group header ("Navigation")
            Array<EditorSettingsBoolField> bools;
        };
        void RegisterEditorSettingsContribution(EditorSettingsContribution contribution)
        {
            m_settingsContributions.PushBack(
                static_cast<EditorSettingsContribution&&>(contribution));
        }
        [[nodiscard]] const Array<EditorSettingsContribution>&
        EditorSettingsContributions() const noexcept
        {
            return m_settingsContributions;
        }
        [[nodiscard]] foundation::resource::ResourceManager* Resources() const noexcept;

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
            Function<foundation::content::Instance*(EditorContext&, foundation::content::Group*)>
                create;
            // Only document-like creations (scenes) become the project's default scene when it
            // is unset; data assets (primitive meshes, materials) never should.
            bool setsDefaultScene = false;
        };

        // === Favorites (pinned asset instances - the browser + picker surface them first) ===

        [[nodiscard]] bool IsFavorite(const Guid& id) const;
        void ToggleFavorite(const Guid& id);
        [[nodiscard]] Span<const Guid> Favorites() const noexcept;
        void SetFavorites(Array<Guid> favorites) { m_favorites = Move(favorites); }
        /// Fired on every toggle (the app persists to the project's Editor/ state).
        Function<void()> OnFavoritesChanged;

        // === Editor clipboard (cross-page: entity subtrees, components) ===
        // One typed slot: `kind` says what the blob is ("entities", "component"); consumers
        // check the kind before parsing. Cleared by overwrite only.

        void SetClipboard(StringView kind, Array<byte> data);
        [[nodiscard]] StringView ClipboardKind() const noexcept { return m_clipboardKind.AsView(); }
        [[nodiscard]] Span<const byte> ClipboardData(StringView kind) const noexcept;

        void RegisterCreator(AssetCreator creator);

        [[nodiscard]] Span<const AssetCreator> Creators() const noexcept;

        // === Open pages ===

        /// Open (or focus) a page editing `instance`: an existing page for the same instance is
        /// activated; otherwise the registry's nearest-type factory creates one. Null if no
        /// factory matches or the instance's type isn't registered.
        EditorPage* OpenPage(foundation::content::Instance& instance);

        /// Adopt an instance-LESS page (the Game tab): same ownership + active-page flow as
        /// OpenPage, but the caller constructs it (no instance, no factory dispatch).
        EditorPage* AdoptPage(UniquePtr<EditorPage> page);

        /// Close a page (the caller is responsible for save-prompting dirty pages first).
        void ClosePage(EditorPage* page);

        [[nodiscard]] Span<const UniquePtr<EditorPage>> OpenPages() const noexcept;

        [[nodiscard]] EditorPage* ActivePage() const noexcept { return m_activePage; }
        void SetActivePage(EditorPage* page);

        // === Edit routing (menu Edit>Undo/Redo -> the active page's stack) ===

        [[nodiscard]] bool CanUndo() const;
        [[nodiscard]] bool CanRedo() const;
        void Undo();
        void Redo();

        // === Selection ===

        /// Global asset selection (asset browser / instance pickers). Entity selection is
        /// per-scene-page.
        [[nodiscard]] Selection<const foundation::content::Instance*>& AssetSelection() noexcept;

        // === Import notifications ===

        /// Subscribe to successful file imports (fired by the import flow AFTER the importer
        /// returned; `options` is the dialog-edited options object, null when none). Used for
        /// post-import steps that live above the importer's layer - e.g. model->prefab
        /// generation, which needs scene machinery the importer library never links.
        void AddImportListener(
            Function<void(foundation::content::Instance&, const pipeline::ImportOptions*)> listener);

        /// Play-in-editor seam: creates the singleton Game page (the player behavior in a
        /// tab). Registered by the scene editor plugin; unset = the Game menu item notifies.
        // Creates a Game tab. `newInstance` = false reuses the app's primary GameInstance (the normal
        // Play); true spins up an ADDITIONAL instance (multi-instance PIE).
        Function<UniquePtr<EditorPage>(bool newInstance)> GamePageFactory;
        /// Stops the Game tab's live run, if any (the embedded app's RequestExit lands
        /// here, deferred to after the page-update loop). Set by the Game page.
        Function<void()> StopGameRun;

        /// Export seam: transcodes a scene/prefab instance's TEXT source stream to the
        /// binary wire for staging. Registered by the scene editor plugin (needs scene
        /// machinery editor.core never links); returns false for non-scene instances or
        /// on failure (the exporter then stages the source verbatim - the runtime sniffs).
        /// MAIN-THREAD only (creates a scratch scene through the SceneSubsystem).
        Function<bool(foundation::content::Instance&, Array<byte>&)> SceneStreamStager;

        /// Export reachability seam: collects the assets a scene/prefab instance references, for the
        /// export closure. Loads the instance over the app's full
        /// manager set, appending its component resource Ref ids to `outResources` and its nested
        /// prefab-instance ids to `outPrefabs`; returns false for non-scene instances or on failure.
        /// Registered by the scene editor plugin (needs scene machinery). MAIN-THREAD only (loads a
        /// scene through the SceneSubsystem), which is exactly why the editor pre-scans with this on
        /// the main thread and hands the resulting guid set to the background export job.
        Function<bool(foundation::content::Instance&, foundation::content::ContentDatabase&,
                      Array<Guid>& /*outResources*/, Array<Guid>& /*outPrefabs*/)>
            SceneRefScanner;

        void NotifyImported(foundation::content::Instance& instance, const pipeline::ImportOptions* options);

        // === Script breakpoints (the debugger) ===
        // Shared editor state: the ScriptPage gutter toggles them per source file+line, and a
        // Game run applies them to its script debugger. Contract-neutral plain data (a future
        // remote debugger consumes the same set).

        struct ScriptBreakpoint
        {
            String file;  // source file name (the section the runtime reports)
            i32 line = 0; // 1-based
        };

        /// Toggle a breakpoint at `file:line`; fires OnBreakpointsChanged.
        void ToggleBreakpoint(StringView file, i32 line);
        [[nodiscard]] bool HasBreakpoint(StringView file, i32 line) const;
        [[nodiscard]] Span<const ScriptBreakpoint> Breakpoints() const noexcept;
        /// Fired on every breakpoint toggle (the gutter repaints; a live run re-applies).
        Function<void()> OnBreakpointsChanged;

        // === Script execution point (paused debugger location) ===
        // Set by a Game run's debugger listener on Breakpoint/Stepped (the innermost stack
        // frame), cleared on resume/stop. The ScriptPage editing that file shows it as the
        // ExecutionLine marker. Version-stamped so consumers can poll cheaply per frame.

        struct ScriptExecutionPoint
        {
            String file;    // source file name (as the runtime reports it)
            i32 line = 0;   // 1-based
            bool active = false;
        };

        void SetScriptExecutionPoint(StringView file, i32 line)
        {
            m_executionPoint.file = String(file);
            m_executionPoint.line = line;
            m_executionPoint.active = true;
            ++m_executionPointVersion;
        }
        void ClearScriptExecutionPoint()
        {
            if (!m_executionPoint.active)
            {
                return;
            }
            m_executionPoint.active = false;
            ++m_executionPointVersion;
        }
        [[nodiscard]] const ScriptExecutionPoint& ScriptExecution() const noexcept
        {
            return m_executionPoint;
        }
        [[nodiscard]] u64 ScriptExecutionVersion() const noexcept
        {
            return m_executionPointVersion;
        }

        /// Debugger value lookup for hover inspection: given an identifier, returns its
        /// display text ("value : Type"), or empty when unavailable (no run, not paused,
        /// unknown name). Installed by the Game page for the run's lifetime; the probe
        /// itself checks pause state. Contract-neutral plain data, like the breakpoint
        /// store - a future remote debugger installs the same shape.
        Function<String(StringView)> ScriptValueProbe;

        // === Status ===

        void SetStatus(StringView text);

    private:
        void NotifyPagesChanged();

        EditorProject* m_project = nullptr;
        EditorJobService* m_jobs = nullptr; // borrowed (app-owned)
        ThumbnailService* m_thumbnails = nullptr; // borrowed (app-owned)
        foundation::resource::ResourceManager* m_resources = nullptr; // borrowed (app-owned)
        foundation::settings::Settings* m_projectEditorSettings = nullptr; // borrowed (app-owned)
        foundation::settings::Settings* m_userEditorSettings = nullptr;    // borrowed (app-owned)
        Array<EditorSettingsContribution> m_settingsContributions;
        pipeline::ImporterRegistry m_importers;                               // borrowed
        EditorPageRegistry m_pageRegistry;
        Array<AssetCreator> m_creators;
        String m_clipboardKind;
        Array<byte> m_clipboard;
        Array<Guid> m_favorites;
        Array<Function<void(foundation::content::Instance&, const pipeline::ImportOptions*)>> m_importListeners;
        Array<UniquePtr<EditorPage>> m_pages;
        EditorPage* m_activePage = nullptr;
        Selection<const foundation::content::Instance*> m_assetSelection;
        Array<ScriptBreakpoint> m_breakpoints; // shared script debugger breakpoints
        ScriptExecutionPoint m_executionPoint; // paused-debugger location (versioned)
        u64 m_executionPointVersion = 0;
    };
}
