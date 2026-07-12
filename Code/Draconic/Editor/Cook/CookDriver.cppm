// Draconic::EditorCook - the `draconic.editor.cook` module.
//
// The incremental cook driver (docs/design/asset-pipeline.md §3/§5). One rule decides
// everything: an asset's RECIPE HASH = H(source envelope bytes, each source file's content,
// builder version, recipe of each content-READ dependency), folded ORDERED into a single u64 -
// dirty <=> product missing or hash mismatch. Deterministic across machines/checkouts (content
// hashes decide; stat (size,mtime) is only a memo to skip re-hashing untouched files, persisted
// in the pipeline DB at .cache/cook.db).
//
// Plan(): walk the source DB -> route instances to builders -> compute recipes (memoized file
// hashes, read-dep chaining with cycle guard) -> dirty set in dependency order + orphan sweep
// (records whose source is gone -> their products are deleted; Traktor's missing piece).
// Execute(): cook dirty items - parallel within dependency levels on the JobSystem - writing
// products into the cooked DB with PRODUCT GUID = SOURCE GUID (user-confirmed), then persist
// the pipeline DB. A failed build keeps the last good product and marks the record failed.
//
// The pipeline DB is one binary file, written whole through the cache mount; corruption or a
// version mismatch degrades to a full re-plan - never wrong output, only wasted work.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.cook;

import draconic.core;
import draconic.content;
import draconic.vfs;
import draconic.editor;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace content = draconic::content;
    namespace vfs = draconic::vfs;

    // === Pipeline DB ===

    /// Per-file memo: content hash + the stat that validated it.
    struct CookFileMemo
    {
        String path;          // sources-mount-relative
        u64 size = 0;
        i64 modifiedTime = 0;
        u64 contentHash = 0;
    };

    /// One source asset's last cook.
    struct CookRecord
    {
        Guid source;
        u64 recipeHash = 0;
        bool failed = false;
        Array<CookFileMemo> files;
        Array<Guid> reads;
        Array<Guid> references;
    };

    inline void Serialize(ISerializer& ar, CookFileMemo& m)
    {
        draconic::core::Serialize(ar, "path", m.path);
        draconic::core::Serialize(ar, "size", m.size);
        draconic::core::Serialize(ar, "mtime", m.modifiedTime);
        draconic::core::Serialize(ar, "hash", m.contentHash);
    }

    /// The persisted pipeline state (.cache/cook.db): source Guid -> CookRecord.
    class CookDb
    {
    public:
        static constexpr u32 kVersion = 1;
        static constexpr StringView kDefaultName = u8"cook.db";

        [[nodiscard]] CookRecord* Find(const Guid& source)
        {
            CookRecord* const* r = m_records.Find(source);
            return (r != nullptr) ? *r : nullptr;
        }

        CookRecord& Upsert(const Guid& source)
        {
            if (CookRecord* existing = Find(source)) { return *existing; }
            auto record = MakeUnique<CookRecord>(DefaultAllocator());
            record->source = source;
            CookRecord* raw = record.Get();
            m_storage.PushBack(Move(record));
            m_records.InsertOrAssign(source, raw);
            return *raw;
        }

        void Remove(const Guid& source)
        {
            m_records.Remove(source);
            for (usize i = 0; i < m_storage.Size(); ++i)
            {
                if (m_storage[i]->source == source)
                {
                    m_storage.RemoveAtSwap(i);
                    return;
                }
            }
        }

        void ForEach(const Function<void(const CookRecord&)>& fn) const
        {
            for (const UniquePtr<CookRecord>& r : m_storage) { fn(*r); }
        }

        [[nodiscard]] usize Count() const noexcept { return m_storage.Size(); }

        /// Load from the cache mount. Missing/corrupt/version-mismatch = empty DB (full re-plan).
        void Load(vfs::IFileSystem& cache, StringView name = kDefaultName)
        {
            m_records.Clear();
            m_storage.Clear();
            UniquePtr<IStream> stream = cache.Open(name, FileMode::Read);
            if (stream.Get() == nullptr) { return; }

            BinarySerializer ar(*stream, SerializeMode::Read);
            u32 version = 0;
            u64 count = 0;
            Serialize(ar, "version", version);
            if (!ar.IsOk() || version != kVersion) { m_records.Clear(); m_storage.Clear(); return; }
            Serialize(ar, "count", count);
            for (u64 i = 0; ar.IsOk() && i < count; ++i)
            {
                CookRecord record;
                SerializeRecord(ar, record);
                if (!ar.IsOk()) { break; }
                Upsert(record.source) = Move(record);
            }
            if (!ar.IsOk()) { m_records.Clear(); m_storage.Clear(); }   // corrupt -> full re-plan
        }

        [[nodiscard]] Status Save(vfs::IFileSystem& cache, StringView name = kDefaultName) const
        {
            vfs::IWritableFileSystem* writable = cache.AsWritable();
            if (writable == nullptr) { return Status{ ErrorCode::NotSupported }; }

            MemoryStream buffer;
            BinarySerializer ar(buffer, SerializeMode::Write);
            u32 version = kVersion;
            u64 count = m_storage.Size();
            Serialize(ar, "version", version);
            Serialize(ar, "count", count);
            for (const UniquePtr<CookRecord>& r : m_storage)
            {
                SerializeRecord(ar, const_cast<CookRecord&>(*r));
            }
            if (!ar.IsOk()) { return Status{ ErrorCode::Internal }; }
            return writable->Save(name, buffer.Bytes());
        }

    private:
        static void SerializeRecord(ISerializer& ar, CookRecord& r)
        {
            Serialize(ar, "source", r.source);
            Serialize(ar, "recipeHash", r.recipeHash);
            Serialize(ar, "failed", r.failed);
            Serialize(ar, "files", r.files);
            Serialize(ar, "reads", r.reads);
            Serialize(ar, "references", r.references);
        }

        HashMap<Guid, CookRecord*> m_records;
        Array<UniquePtr<CookRecord>> m_storage;
    };

    // === Recipe hashing ===

    namespace detail
    {
        // Ordered fold: every field is tagged so reorderings/offsets can't cancel out
        // (Traktor's commutative sums are the cautionary tale).
        inline u64 FoldHash(u64 seed, u64 tag, u64 value)
        {
            u64 h = seed;
            h = HashBytes(&tag, sizeof(tag), h);
            h = HashBytes(&value, sizeof(value), h);
            return h;
        }

        inline u64 HashStream(IStream& stream)
        {
            byte chunk[64 * 1024];
            u64 h = HashBytes(nullptr, 0);
            for (;;)
            {
                const u64 read = stream.Read(chunk, sizeof(chunk));
                if (read == 0) { break; }
                h = HashBytes(chunk, static_cast<usize>(read), h);
            }
            return h;
        }
    }

    // === Plan / Execute ===

    struct CookItem
    {
        Guid source;
        String path;              // source instance path (progress display + product placement)
        IAssetBuilder* builder = nullptr;
        RefPtr<ISerializable> asset;   // deserialized source object (kept for Build)
        u64 recipeHash = 0;
        AssetDependencies deps;
        i32 level = 0;            // dependency depth (items cook level-by-level, parallel within)
        draconic::content::Instance* product = nullptr;   // pre-created SERIALLY before workers run
    };

    struct CookPlan
    {
        Array<CookItem> dirty;    // in dependency order (level ascending)
        Array<Guid> orphans;      // records whose source is gone -> products swept
        usize upToDate = 0;
        usize unbuildable = 0;    // instances with no registered builder (informational)
    };

    struct CookProgress
    {
        // (done, total, sourcePath, ok) after each item completes.
        Function<void(usize, usize, StringView, bool)> onItem;
    };

    struct CookStats
    {
        usize cooked = 0;
        usize failed = 0;
        usize orphansSwept = 0;
        Array<Guid> cookedProducts;   // successfully (re)built products - hot-reload input
    };

    class CookDriver
    {
    public:
        CookDriver(content::ContentDatabase& sourceDb, content::ContentDatabase& cookedDb,
                   BuilderRegistry& builders, vfs::IFileSystem* sourcesMount,
                   vfs::IFileSystem* cacheMount, JobSystem* jobs = nullptr)
            : m_sourceDb(&sourceDb), m_cookedDb(&cookedDb), m_builders(&builders)
            , m_sources(sourcesMount), m_cache(cacheMount), m_jobs(jobs)
        {
            if (m_cache != nullptr) { m_db.Load(*m_cache); }
        }

        [[nodiscard]] CookDb& Db() noexcept { return m_db; }

        /// Compute the dirty set (+ orphans). `force` marks every buildable instance dirty.
        [[nodiscard]] CookPlan Plan(bool force = false)
        {
            CookPlan plan;
            m_recipeMemo.Clear();

            // Gather every buildable source instance.
            Array<content::Instance*> instances;
            CollectInstances(m_sourceDb->RootGroup(), instances);

            HashMap<Guid, i32> levels;   // read-dep depth per source (0 = no reads)
            for (content::Instance* instance : instances)
            {
                IAssetBuilder* builder = m_builders->FindByTypeName(instance->TypeName());
                if (builder == nullptr) { ++plan.unbuildable; continue; }

                CookItem item;
                item.source = instance->Id();
                item.path = instance->Path();
                item.builder = builder;
                item.asset = instance->ReadObject();
                if (item.asset.Get() == nullptr)
                {
                    // Deserialization failed - usually a source written by an OLDER schema
                    // (no asset compatibility by policy): delete + re-import it.
                    DRACONIC_LOG_WARNING(u8"Cook",
                        u8"'{}' failed to deserialize (stale schema? delete + re-import)", item.path);
                    ++plan.unbuildable;
                    continue;
                }
                Asset* asset = Cast<Asset>(item.asset.Get());
                if (asset == nullptr)
                {
                    DRACONIC_LOG_WARNING(u8"Cook", u8"'{}' has a builder but is not an Asset", item.path);
                    ++plan.unbuildable;
                    continue;
                }

                AssetBuildContext scanCtx;
                scanCtx.sources = m_sources;
                scanCtx.db = m_sourceDb;
                builder->ScanDependencies(*asset, scanCtx, item.deps);

                item.recipeHash = ComputeRecipe(*instance, *asset, *builder, item.deps, 0);
                item.level = ReadDepth(item.source, item.deps, levels, 0);

                const CookRecord* record = m_db.Find(item.source);
                const bool productExists = m_cookedDb->GetInstance(item.source) != nullptr;
                const bool clean = !force && record != nullptr && !record->failed
                                && record->recipeHash == item.recipeHash && productExists;
                if (clean) { ++plan.upToDate; }
                else { plan.dirty.PushBack(Move(item)); }
            }

            // Dependency order: stable sort by level (reads cook before their consumers).
            SortByLevel(plan.dirty);

            // Orphan sweep: records whose source instance no longer exists.
            Array<Guid> orphans;
            m_db.ForEach([&](const CookRecord& record) {
                if (m_sourceDb->GetInstance(record.source) == nullptr)
                {
                    orphans.PushBack(record.source);
                }
            });
            plan.orphans = Move(orphans);
            return plan;
        }

        /// Cook the plan. Items run level-by-level; within a level in parallel when a
        /// JobSystem was provided. Persists the pipeline DB at the end.
        CookStats Execute(CookPlan& plan, const CookProgress* progress = nullptr)
        {
            CookStats stats;

            // Sweep orphans first (their products must not survive the cook).
            for (const Guid& orphan : plan.orphans)
            {
                if (m_cookedDb->GetInstance(orphan) != nullptr)
                {
                    (void)m_cookedDb->DeleteInstance(orphan);
                }
                m_db.Remove(orphan);
                ++stats.orphansSwept;
            }

            // Pre-create every product instance ON THIS THREAD: the content DB's group tree
            // and GUID index are not thread-safe, so all DB MUTATION happens before the
            // parallel build loop - workers then only read the DB and write their own
            // instance's files. (Without this, the first big parallel cook segfaults on
            // concurrent CreateInstanceWithId - found by a 30-asset model drop.)
            for (CookItem& item : plan.dirty)
            {
                item.product = EnsureProduct(item);
            }

            Array<bool> results;
            results.Resize(plan.dirty.Size());
            usize done = 0;
            usize begin = 0;
            while (begin < plan.dirty.Size())
            {
                // The half-open range of the current dependency level.
                usize end = begin;
                while (end < plan.dirty.Size() && plan.dirty[end].level == plan.dirty[begin].level)
                {
                    ++end;
                }

                if (m_jobs != nullptr && end - begin > 1)
                {
                    m_jobs->ParallelFor(static_cast<u32>(end - begin), [&, begin](u32 i) {
                        results[begin + i] = CookItem_(plan.dirty[begin + i]);
                    });
                }
                else
                {
                    for (usize i = begin; i < end; ++i) { results[i] = CookItem_(plan.dirty[i]); }
                }

                for (usize i = begin; i < end; ++i)
                {
                    results[i] ? ++stats.cooked : ++stats.failed;
                    if (results[i]) { stats.cookedProducts.PushBack(plan.dirty[i].source); }
                    ++done;
                    if (progress != nullptr && progress->onItem)
                    {
                        progress->onItem(done, plan.dirty.Size(), plan.dirty[i].path.AsView(), results[i]);
                    }
                }
                begin = end;
            }

            if (m_cache != nullptr)
            {
                const Status saved = m_db.Save(*m_cache);
                if (!saved.IsOk()) { DRACONIC_LOG_WARNING(u8"Cook", u8"pipeline db save failed"); }
            }
            return stats;
        }

    private:
        static void CollectInstances(content::Group* group, Array<content::Instance*>& out)
        {
            if (group == nullptr) { return; }
            for (content::Instance* instance : group->Instances()) { out.PushBack(instance); }
            for (content::Group* child : group->Groups()) { CollectInstances(child, out); }
        }

        // Content hash of one source file through the mount, memoized by (size, mtime) against
        // the previous record when the mount supports stat.
        [[nodiscard]] u64 HashSourceFile(StringView path, const CookRecord* previous,
                                         Array<CookFileMemo>& outMemos)
        {
            CookFileMemo memo;
            memo.path = String(path);

            vfs::FileStatInfo stat;
            const bool hasStat = m_sources != nullptr && m_sources->AsStat() != nullptr
                              && m_sources->AsStat()->Stat(path, stat);
            if (hasStat && previous != nullptr)
            {
                for (const CookFileMemo& old : previous->files)
                {
                    if (old.path == path && old.size == stat.size
                        && old.modifiedTime == stat.modifiedTime)
                    {
                        memo = old;   // untouched since last cook: reuse the content hash
                        outMemos.PushBack(Move(memo));
                        return outMemos[outMemos.Size() - 1].contentHash;
                    }
                }
            }

            u64 hash = 0;   // missing file hashes as 0 (the recipe still changes when it appears)
            if (m_sources != nullptr)
            {
                UniquePtr<IStream> stream = m_sources->Open(path, FileMode::Read);
                if (stream.Get() != nullptr) { hash = detail::HashStream(*stream); }
            }
            memo.contentHash = hash;
            if (hasStat)
            {
                memo.size = stat.size;
                memo.modifiedTime = stat.modifiedTime;
            }
            outMemos.PushBack(Move(memo));
            return hash;
        }

        // The recipe hash (design §3): envelope bytes + file contents + builder version +
        // read-dep recipes, folded ordered. Guarded against read-dep cycles.
        [[nodiscard]] u64 ComputeRecipe(content::Instance& instance, const Asset& asset,
                                        IAssetBuilder& builder, const AssetDependencies& deps,
                                        i32 depth)
        {
            if (const u64* memo = m_recipeMemo.Find(instance.Id())) { return *memo; }
            if (depth > 64)
            {
                DRACONIC_LOG_WARNING(u8"Cook", u8"read-dependency cycle at '{}'", instance.Path());
                return 0;
            }

            u64 h = HashBytes(nullptr, 0);

            // 1. The source envelope (import settings + identity + embedded stream directory).
            {
                UniquePtr<IStream> envelope = instance.OpenEnvelope();
                const u64 envelopeHash = (envelope.Get() != nullptr) ? detail::HashStream(*envelope) : 0;
                h = detail::FoldHash(h, 'E', envelopeHash);
            }

            // 2. Source files: the implicit fileName + declared extras, in declaration order.
            const CookRecord* previous = m_db.Find(instance.Id());
            Array<CookFileMemo> memos;
            if (!asset.fileName.IsEmpty())
            {
                h = detail::FoldHash(h, 'F', HashSourceFile(asset.fileName.AsView(), previous, memos));
            }
            for (const String& file : deps.files)
            {
                h = detail::FoldHash(h, 'F', HashSourceFile(file.AsView(), previous, memos));
            }
            m_pendingMemos.InsertOrAssign(instance.Id(), Move(memos));

            // 2b. Declared embedded streams (sidecar files the envelope hash doesn't cover).
            for (const String& streamName : deps.sourceStreams)
            {
                UniquePtr<IStream> stream = instance.ReadData(streamName.AsView());
                h = detail::FoldHash(h, 'S',
                    (stream.Get() != nullptr) ? detail::HashStream(*stream) : 0);
            }

            // 3. Builder version.
            h = detail::FoldHash(h, 'V', builder.Version());

            // 4. Read deps (sorted by Guid for determinism), chained recursively.
            Array<Guid> reads(deps.reads);
            SortGuids(reads);
            for (const Guid& read : reads)
            {
                u64 readRecipe = 0;
                if (content::Instance* dep = m_sourceDb->GetInstance(read))
                {
                    if (IAssetBuilder* depBuilder = m_builders->FindByTypeName(dep->TypeName()))
                    {
                        RefPtr<ISerializable> depObject = dep->ReadObject();
                        if (Asset* depAsset = Cast<Asset>(depObject.Get()))
                        {
                            AssetBuildContext scanCtx;
                            scanCtx.sources = m_sources;
                            scanCtx.db = m_sourceDb;
                            AssetDependencies depDeps;
                            depBuilder->ScanDependencies(*depAsset, scanCtx, depDeps);
                            readRecipe = ComputeRecipe(*dep, *depAsset, *depBuilder, depDeps, depth + 1);
                        }
                    }
                }
                h = detail::FoldHash(h, 'R', readRecipe);
                h = detail::FoldHash(h, 'G', read.high);
                h = detail::FoldHash(h, 'g', read.low);
            }

            m_recipeMemo.InsertOrAssign(instance.Id(), h);
            return h;
        }

        // Depth of the read-dependency chain (level 0 cooks first).
        [[nodiscard]] i32 ReadDepth(const Guid& source, const AssetDependencies& deps,
                                    HashMap<Guid, i32>& levels, i32 depth)
        {
            if (const i32* known = levels.Find(source)) { return *known; }
            if (depth > 64) { return depth; }   // cycle guard (already warned in ComputeRecipe)
            i32 level = 0;
            for (const Guid& read : deps.reads)
            {
                content::Instance* dep = m_sourceDb->GetInstance(read);
                if (dep == nullptr) { continue; }
                IAssetBuilder* depBuilder = m_builders->FindByTypeName(dep->TypeName());
                if (depBuilder == nullptr) { continue; }
                RefPtr<ISerializable> depObject = dep->ReadObject();
                Asset* depAsset = Cast<Asset>(depObject.Get());
                if (depAsset == nullptr) { continue; }
                AssetBuildContext scanCtx;
                scanCtx.sources = m_sources;
                scanCtx.db = m_sourceDb;
                AssetDependencies depDeps;
                depBuilder->ScanDependencies(*depAsset, scanCtx, depDeps);
                const i32 depLevel = ReadDepth(read, depDeps, levels, depth + 1);
                if (depLevel + 1 > level) { level = depLevel + 1; }
            }
            levels.InsertOrAssign(source, level);
            return level;
        }

        static void SortByLevel(Array<CookItem>& items)
        {
            // Insertion sort (stable; plans are small and mostly ordered).
            for (usize i = 1; i < items.Size(); ++i)
            {
                usize j = i;
                while (j > 0 && items[j - 1].level > items[j].level)
                {
                    CookItem tmp = Move(items[j - 1]);
                    items[j - 1] = Move(items[j]);
                    items[j] = Move(tmp);
                    --j;
                }
            }
        }

        static void SortGuids(Array<Guid>& guids)
        {
            for (usize i = 1; i < guids.Size(); ++i)
            {
                usize j = i;
                auto less = [](const Guid& a, const Guid& b) {
                    return a.high < b.high || (a.high == b.high && a.low < b.low);
                };
                while (j > 0 && less(guids[j], guids[j - 1]))
                {
                    const Guid tmp = guids[j - 1];
                    guids[j - 1] = guids[j];
                    guids[j] = tmp;
                    --j;
                }
            }
        }

        // The product instance for an item: mirrored path, SAME guid, stamped with the
        // builder's product type. MUTATES the cooked DB - main thread only (see Execute).
        [[nodiscard]] content::Instance* EnsureProduct(const CookItem& item)
        {
            if (content::Instance* existing = m_cookedDb->GetInstance(item.source)) { return existing; }
            content::Instance* source = m_sourceDb->GetInstance(item.source);
            const TypeInfo* productType = (item.builder != nullptr) ? item.builder->ProductType() : nullptr;
            if (source == nullptr || productType == nullptr) { return nullptr; }
            content::Group* group = MirrorGroup(source->OwningGroup());
            return group->CreateInstanceWithId(item.source, source->Name(), *productType);
        }

        // Cook one item into its pre-created product and update its record. Runs on WORKER
        // threads: no DB mutation here - only reads + the product's own file writes.
        [[nodiscard]] bool CookItem_(CookItem& item)
        {
            content::Instance* source = m_sourceDb->GetInstance(item.source);
            Asset* asset = Cast<Asset>(item.asset.Get());
            content::Instance* product = item.product;
            if (source == nullptr || asset == nullptr || product == nullptr) { return false; }

            AssetBuildContext ctx;
            ctx.sources = m_sources;
            ctx.source = source;
            ctx.output = product;
            ctx.db = m_cookedDb;   // cross-refs resolve against already-cooked products
            const Status built = item.builder->Build(*asset, ctx);

            // Record: recipe + memoized file hashes + deps; failures keep the last good
            // product but stay dirty (failed record never satisfies a clean check).
            ScopedLock lock(m_recordMutex);
            CookRecord& record = m_db.Upsert(item.source);
            record.recipeHash = item.recipeHash;
            record.failed = !built.IsOk();
            if (Array<CookFileMemo>* memos = m_pendingMemos.Find(item.source))
            {
                record.files = Move(*memos);
            }
            record.reads = item.deps.reads;
            record.references = item.deps.references;
            if (!built.IsOk())
            {
                DRACONIC_LOG_ERROR(u8"Cook", u8"'{}' failed to cook", item.path);
            }
            return built.IsOk();
        }

        // The cooked-DB group mirroring the source instance's group path.
        [[nodiscard]] content::Group* MirrorGroup(content::Group& sourceGroup)
        {
            Array<StringView> chain;
            for (content::Group* g = &sourceGroup; g != nullptr && !g->Name().IsEmpty(); g = g->Parent())
            {
                chain.PushBack(g->Name());
            }
            content::Group* group = m_cookedDb->RootGroup();
            for (usize i = chain.Size(); i > 0; --i)
            {
                group = group->CreateGroup(chain[i - 1]);
            }
            return group;
        }

        content::ContentDatabase* m_sourceDb;
        content::ContentDatabase* m_cookedDb;
        BuilderRegistry* m_builders;
        vfs::IFileSystem* m_sources;   // nullable: embedded-data projects have no source files
        vfs::IFileSystem* m_cache;     // nullable: no persistence (tests / one-shot cooks)
        JobSystem* m_jobs;             // nullable: serial execution

        CookDb m_db;
        HashMap<Guid, u64> m_recipeMemo;                    // per-Plan recipe cache
        HashMap<Guid, Array<CookFileMemo>> m_pendingMemos;  // file memos gathered during Plan
        Mutex m_recordMutex;                                // record updates from worker threads
    };
}
