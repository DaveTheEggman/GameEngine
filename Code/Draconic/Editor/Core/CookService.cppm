// Draconic::EditorCore - :cook_service partition.
//
// EditorCookService: the in-editor face of the cook driver (asset-pipeline design §6). Owns the
// project's sources/.cache mounts + a CookDriver over the project DBs, and runs cooks on a
// BACKGROUND thread (one at a time - Traktor's build lock): the UI stays live, progress
// messages queue through a mutex and drain on the main thread via Update(). Cook badges give
// the Assets panel a cheap per-instance state without recomputing recipes (exact dirtiness is
// the driver's job at cook time):
//   NoBuilder - the instance's type has no registered builder (scenes, raw data)
//   Failed    - the last cook of this asset failed (Console has the log)
//   Missing   - no record or no product yet (never cooked, or swept)
//   Cooked    - record + product exist (a stale recipe still shows Cooked until the next cook)
//
// The service also watches the project's Sources/ tree (the native mount's stat-sweep
// IChangeSource, polled every couple of seconds from Update): an external edit of a source
// file queues an automatic incremental cook - the driver's plan recomputes exactly what the
// change dirtied. After every cook, LastCookedProducts() lists the rebuilt product guids so the
// application can hot-reload them through the ResourceManager (proxy swap in live scenes).
//
// Known v1 hazard (documented, Traktor shares it): editing source assets WHILE a cook runs
// races the driver's source reads; the UI disables cook triggers during a cook but does not
// lock edits.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.core:cook_service;

import draconic.core;
import draconic.content;
import draconic.vfs;
import draconic.editor;
import draconic.editor.cook;
import :project;

using namespace draconic::core;

export namespace draconic::editor
{
    enum class CookBadge : u8 { NoBuilder, Cooked, Missing, Failed };

    class EditorCookService
    {
    public:
        /// Fired on the main thread (from Update) when a cook finishes.
        Function<void()> OnCookFinished;

        ~EditorCookService() { Shutdown(); }

        /// Wire to the open project. `builders` must outlive the service (the executable
        /// assembles the registry before the project opens).
        void Initialize(EditorProject& project, BuilderRegistry& builders)
        {
            Shutdown();
            m_project = &project;
            m_builders = &builders;
            m_sources = MakeUnique<draconic::vfs::NativeFileSystem>(DefaultAllocator(),
                project.SourcesRoot().AsView());
            m_cache = MakeUnique<draconic::vfs::NativeFileSystem>(DefaultAllocator(),
                project.CacheRoot().AsView());
            m_jobs = MakeUnique<JobSystem>(DefaultAllocator());
            m_driver = MakeUnique<CookDriver>(DefaultAllocator(),
                project.SourceDb(), project.CookedDb(), builders,
                m_sources.Get(), m_cache.Get(), m_jobs.Get());

            // Watch Sources/ for external edits (stat sweep; throttled from Update).
            if (draconic::vfs::IWatchableFileSystem* watchable = m_sources->AsWatchable())
            {
                m_watcher = watchable->ChangeSource();
                if (m_watcher != nullptr) { m_watcher->Track(u8""); }
            }
            m_lastWatchPoll = TicksToSeconds(GetTicks());
        }

        void Shutdown()
        {
            JoinWorker();
            m_watcher = nullptr;   // owned by the sources mount
            m_driver.Reset();
            m_jobs.Reset();
            m_cache.Reset();
            m_sources.Reset();
            m_project = nullptr;
        }

        [[nodiscard]] bool IsReady() const noexcept { return m_driver.Get() != nullptr; }
        [[nodiscard]] bool IsCooking() const noexcept { return m_cooking.load(); }

        /// External contributor to the mutation lock (wired by the app): e.g. a background
        /// EXPORT job reads the source DB structure and packs cooked files from its worker,
        /// so DB mutations and new cooks must hold off exactly like during a cook.
        Function<bool()> ExternalMutationLock;

        /// True while ANY background reader of the DBs is in flight (a cook, or an external
        /// job via ExternalMutationLock). Every structural-mutation gate and the watcher key
        /// on THIS, not on IsCooking alone.
        [[nodiscard]] bool MutationLocked() const
        {
            return IsCooking() || (ExternalMutationLock && ExternalMutationLock());
        }

        /// Bumped when a cook finishes - UI (badges) refreshes off it.
        [[nodiscard]] u64 Revision() const noexcept { return m_revision; }

        /// Kick a background cook. A request while one is running is REMEMBERED and re-
        /// issued when it finishes (previously it was silently dropped - a save-during-cook
        /// lost its recook). `force` = rebuild all.
        void RequestCook(bool force = false)
        {
            if (!IsReady()) { return; }
            if (MutationLocked())   // cook running OR an external job is reading the DBs
            {
                m_pendingCook = true;
                m_pendingForce = m_pendingForce || force;
                return;
            }
            JoinWorker();   // reap the previous worker's handle

            // Three phases so the UI never stalls AND nothing races:
            //   1. Plan on the WORKER - it only READS the DBs, and their structure is frozen
            //      while IsCooking(): every main-thread structural mutation (import, delete,
            //      rename, create) gates through RunWhenIdle. (Planning hashes every source
            //      file - on the main thread it froze the editor for whole rebuilds.)
            //   2. PrepareProducts on the MAIN thread (Update drains m_planReady) - the only
            //      phase that MUTATES the cooked DB, which main-thread resource loads read at
            //      any time.
            //   3. Builds on the worker (no DB queries - snapshotted pointers).
            m_cooking.store(true);
            CookDriver* driver = m_driver.Get();
            EditorCookService* self = this;
            m_worker = MakeUnique<Thread>(DefaultAllocator(), [self, driver, force]() {
                self->m_plan = driver->Plan(force);
                self->m_planReady.store(true);
            });
        }

        /// Scoped cook: the given source instances plus their dependency closure (a group's
        /// ids, one asset, ...). Same three-phase flow as RequestCook; queued via RunWhenIdle
        /// when a cook is already in flight. No orphan sweep (whole-project concern).
        void RequestCookFor(Array<Guid> roots, bool force = false)
        {
            if (!IsReady() || roots.IsEmpty()) { return; }
            if (MutationLocked())
            {
                EditorCookService* self = this;
                RunWhenIdle(Function<void()>{ [self, roots = Move(roots), force]() mutable {
                    self->RequestCookFor(Move(roots), force);
                } });
                return;
            }
            JoinWorker();
            m_cooking.store(true);
            CookDriver* driver = m_driver.Get();
            EditorCookService* self = this;
            m_worker = MakeUnique<Thread>(DefaultAllocator(),
                                          [self, driver, roots = Move(roots), force]() {
                self->m_plan = driver->PlanFor(Span<const Guid>{ roots.Data(), roots.Size() }, force);
                self->m_planReady.store(true);
            });
        }

        /// Phase 2+3 hand-off: PrepareProducts on the main thread, then the build worker.
        void StartBuilds()
        {
            JoinWorker();   // the plan worker has finished (m_planReady was set)
            m_driver->PrepareProducts(m_plan);
            const usize total = m_plan.dirty.Size();
            Post(FormatPlanned(total, m_plan.orphans.Size()));

            CookDriver* driver = m_driver.Get();
            EditorCookService* self = this;
            m_worker = MakeUnique<Thread>(DefaultAllocator(), [self, driver, total]() {
                CookPlan& plan = self->m_plan;
                CookProgress progress;
                progress.onItem = [self, total](usize done, usize, StringView path, bool ok) {
                    String line(ok ? u8"cooked " : u8"FAILED ");
                    line.Append(path);
                    line.Append(u8" (");
                    AppendCount(line, done);
                    line.PushBack(utf8char('/'));
                    AppendCount(line, total);
                    line.PushBack(utf8char(')'));
                    self->Post(Move(line));
                };
                CookStats stats = driver->ExecuteBuilds(plan, &progress);
                String done(u8"cook finished: ");
                AppendCount(done, stats.cooked);
                done.Append(u8" cooked, ");
                AppendCount(done, stats.failed);
                done.Append(u8" failed");
                self->Post(Move(done));
                {
                    ScopedLock lock(self->m_queueMutex);
                    self->m_lastCooked = Move(stats.cookedProducts);
                    self->m_lastCookedCount = stats.cooked;
                    self->m_lastFailedCount = stats.failed;
                }
                self->m_cooking.store(false);
                self->m_finishedPending.store(true);
            });
        }

        /// Main-thread pump: drains progress messages into `status` (e.g. the status bar +
        /// console) and fires OnCookFinished after a cook completes.
        void Update(const Function<void(StringView)>& status)
        {
            // Phase hand-off: the plan worker finished - run the main-thread product
            // pre-create and kick the build worker.
            if (m_planReady.exchange(false)) { StartBuilds(); }

            Array<String> drained;
            {
                ScopedLock lock(m_queueMutex);
                for (String& s : m_queue) { drained.PushBack(Move(s)); }
                m_queue.Clear();
            }
            for (const String& line : drained)
            {
                DRACONIC_LOG_INFO(u8"Cook", u8"{}", line);
                if (status) { status(line.AsView()); }
            }
            if (m_finishedPending.exchange(false))
            {
                ++m_revision;
                {
                    ScopedLock lock(m_queueMutex);
                    m_lastCookedMain = Move(m_lastCooked);
                    m_lastCookedCountMain = m_lastCookedCount;
                    m_lastFailedCountMain = m_lastFailedCount;
                }
                if (OnCookFinished) { OnCookFinished(); }
            }

            // Deferred source-DB mutations (imports/deletes queued while the cook worker was
            // reading snapshotted instances) + a cook request that arrived mid-cook.
            if (!MutationLocked())
            {
                if (!m_idleQueue.IsEmpty())
                {
                    Array<Function<void()>> drained = Move(m_idleQueue);
                    m_idleQueue = Array<Function<void()>>{};
                    for (Function<void()>& action : drained) { action(); }
                }
                if (m_pendingCook)
                {
                    m_pendingCook = false;
                    const bool force = m_pendingForce;
                    m_pendingForce = false;
                    RequestCook(force);
                }
            }

            // Watcher: throttled stat sweep; any source change queues an incremental cook.
            // Held off while ANY background DB reader runs (a mid-export cook would rewrite
            // the very cooked files the export job is packing).
            if (m_watcher != nullptr && !MutationLocked())
            {
                const f64 now = TicksToSeconds(GetTicks());
                if (now - m_lastWatchPoll >= kWatchPollSeconds)
                {
                    m_lastWatchPoll = now;
                    m_watchChanged.Clear();
                    if (m_watcher->Poll(m_watchChanged))
                    {
                        DRACONIC_LOG_INFO(u8"Cook", u8"{} source file(s) changed - recooking",
                                          m_watchChanged.Size());
                        RequestCook(false);
                    }
                }
            }
        }

        /// Defer a MAIN-THREAD source-DB mutation (import/delete) until no cook is in
        /// flight: the worker reads instance pointers snapshotted at plan time, so creating
        /// or deleting instances mid-cook is a race. Runs immediately when idle. Main-thread
        /// only (like every other DB entry point).
        void RunWhenIdle(Function<void()> action)
        {
            if (!MutationLocked()) { if (action) { action(); } return; }
            m_idleQueue.PushBack(Move(action));
        }

        /// Products rebuilt by the most recent cook (valid after OnCookFinished fires, until
        /// the next cook finishes). The app hot-reloads these through the ResourceManager.
        [[nodiscard]] Span<const Guid> LastCookedProducts() const noexcept
        {
            return Span<const Guid>(m_lastCookedMain.Data(), m_lastCookedMain.Size());
        }

        /// Pass/fail counts of the most recent cook (valid when OnCookFinished fires, like
        /// LastCookedProducts).
        [[nodiscard]] usize LastCookedCount() const noexcept { return m_lastCookedCountMain; }
        [[nodiscard]] usize LastFailedCount() const noexcept { return m_lastFailedCountMain; }

        /// Cheap per-instance cook state for the Assets panel (no recipe recompute).
        [[nodiscard]] CookBadge BadgeFor(draconic::content::Instance& instance)
        {
            if (!IsReady() || m_builders == nullptr) { return CookBadge::NoBuilder; }
            if (m_builders->FindByTypeName(instance.TypeName()) == nullptr)
            {
                return CookBadge::NoBuilder;
            }
            if (IsCooking()) { return CookBadge::Missing; }   // db is the worker's during a cook
            const CookRecord* record = m_driver->Db().Find(instance.Id());
            if (record != nullptr && record->failed) { return CookBadge::Failed; }
            const bool productExists = m_project->CookedDb().GetInstance(instance.Id()) != nullptr;
            return (record != nullptr && productExists) ? CookBadge::Cooked : CookBadge::Missing;
        }

    private:
        void Post(String message)
        {
            ScopedLock lock(m_queueMutex);
            m_queue.PushBack(Move(message));
        }

        void JoinWorker()
        {
            if (m_worker)
            {
                m_worker->Join();
                m_worker.Reset();
            }
        }

        static void AppendCount(String& out, usize value)
        {
            utf8char digits[24];
            i32 n = 0;
            usize v = value;
            do { digits[n++] = static_cast<utf8char>('0' + v % 10); v /= 10; } while (v > 0 && n < 24);
            while (n > 0) { out.PushBack(digits[--n]); }
        }

        [[nodiscard]] static String FormatPlanned(usize dirty, usize orphans)
        {
            String s(u8"cooking ");
            AppendCount(s, dirty);
            s.Append(u8" asset(s)");
            if (orphans > 0)
            {
                s.Append(u8", sweeping ");
                AppendCount(s, orphans);
                s.Append(u8" orphan(s)");
            }
            return s;
        }

        static constexpr f64 kWatchPollSeconds = 2.0;

        EditorProject* m_project = nullptr;       // borrowed
        BuilderRegistry* m_builders = nullptr;    // borrowed (exe-assembled)
        UniquePtr<draconic::vfs::NativeFileSystem> m_sources;
        UniquePtr<draconic::vfs::NativeFileSystem> m_cache;
        UniquePtr<JobSystem> m_jobs;
        UniquePtr<CookDriver> m_driver;
        UniquePtr<Thread> m_worker;

        Atomic<bool> m_cooking{ false };
        Atomic<bool> m_finishedPending{ false };
        u64 m_revision = 0;
        Mutex m_queueMutex;
        Array<String> m_queue;
        Array<Guid> m_lastCooked;        // written by the worker under m_queueMutex
        Array<Guid> m_lastCookedMain;    // main-thread copy (LastCookedProducts)
        usize m_lastCookedCount = 0;     // worker-written, under m_queueMutex
        usize m_lastFailedCount = 0;
        usize m_lastCookedCountMain = 0; // main-thread copies
        usize m_lastFailedCountMain = 0;
        Array<Function<void()>> m_idleQueue;   // main-thread deferred mutations (RunWhenIdle)
        bool m_pendingCook = false;            // a RequestCook arrived while cooking
        bool m_pendingForce = false;
        CookPlan m_plan;                       // worker-planned, main-prepared, worker-built
        Atomic<bool> m_planReady{ false };
        draconic::vfs::IChangeSource* m_watcher = nullptr;   // borrowed (sources mount owns it)
        Array<String> m_watchChanged;
        f64 m_lastWatchPoll = 0.0;
    };
}
