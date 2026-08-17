// Editor Core - :thumbnail_service partition
//
// Asset thumbnails (asset-thumbnails.md P1): an editor-only service that resolves a content
// instance's Guid to a small preview drawable. Icon-first display is the CONSUMER's job - Get()
// returns empty until a thumbnail exists and OnThumbnailReady fires the swap-in; nothing here
// ever blocks the UI.
//
// Mechanics: per-asset-type generators (registered by the app; unknown types simply keep their
// icon) run on the EditorJobService LIGHT lane; results land in a content-keyed disk cache
// (<project>/.cache/thumbs/<guid>-<recipeHash>.png - a stale hash is detected by filename
// mismatch and the old file replaced; deleting the directory is always safe) and a RAM map of
// OWNING drawables. OwnedThumbnailDrawable owns its pixels, so map eviction can never dangle a
// drawable already handed to the UI (the bind-group value-keyed-cache lesson: renderer caches
// key on ImageData::InstanceId, and the pixels live exactly as long as the drawable).
//
// Threading: Get/Invalidate/OnThumbnailReady are MAIN-thread; generation + PNG IO run on the
// light worker; completion fires on the main thread from EditorJobService::Update. In-flight
// jobs use the heap-slot lifetime pattern (editor-jobs.md) so project close or service reset
// mid-flight is safe.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

export module editor.core:thumbnail_service;

import foundation.core;
import foundation.content;
import foundation.image;
import foundation.image.io;
import foundation.ui;
import foundation.vfs;
import :job_service;

using namespace foundation::core;
namespace content = foundation::content;
namespace image = foundation::image;
namespace ui = foundation::ui;

export namespace editor
{
    /// A thumbnail drawable that OWNS its pixels: the image lives exactly as long as the
    /// drawable, so the service's RAM map can drop entries freely while the UI still holds refs.
    class OwnedThumbnailDrawable final : public ui::ImageDrawable
    {
        RTTI_OBJECT(OwnedThumbnailDrawable, ui::ImageDrawable)
    public:
        explicit OwnedThumbnailDrawable(image::Image pixels) : m_pixels(Move(pixels))
        {
            Image = &m_pixels;
        }

    private:
        image::Image m_pixels;
    };

    RTTI_DEFINE_OBJECT(OwnedThumbnailDrawable, "rtti::editor")

    /// One per-asset-type thumbnail producer (asset-thumbnails.md). Split across threads:
    /// Prepare runs on the MAIN thread and gathers everything the worker needs (content
    /// Instance/DB access is main-thread-only - a cook or delete can run concurrently with the
    /// light lane); Generate runs on the LIGHT worker over that payload: CPU only, no UI, no
    /// GPU (offscreen renders are the P2 preview-bake path). The output image should already
    /// be thumbnail-sized (the service saves it verbatim).
    class IThumbnailGenerator
    {
    public:
        virtual ~IThumbnailGenerator() = default;
        /// The content asset-type names this generator covers (e.g. "TextureAsset").
        [[nodiscard]] virtual Span<const StringView> AssetTypeNames() const = 0;
        /// MAIN thread: read the instance's source data into a worker-safe payload.
        /// `sources` is the project's Sources/ mount - imported source FILES live there
        /// (mount-relative Asset::fileName paths), embedded data lives in instance streams.
        [[nodiscard]] virtual Status Prepare(content::Instance& instance,
                                             foundation::vfs::IFileSystem& sources,
                                             Array<byte>& payload) = 0;
        /// LIGHT worker: produce the thumbnail pixels from the prepared payload.
        [[nodiscard]] virtual Status Generate(Span<const byte> payload, image::Image& out) = 0;
    };

    /// The thumbnail service (asset-thumbnails.md P1). One per open project - the app Configures
    /// it on project open and Resets it on close.
    class ThumbnailService
    {
    public:
        /// Thumbnail edge size (square, aspect-fit letterboxed by the generators' helper).
        static constexpr u32 kThumbnailSize = 128;
        /// Scheduling budget (the cross-engine lesson): at most this many loads in flight; a
        /// bind sweep over a big folder trickles instead of flooding the light lane - the NEXT
        /// Get for a skipped id (every bind re-queries) schedules it once a slot frees.
        static constexpr usize kMaxInFlight = 8;

        [[nodiscard]] usize GeneratorCount() const noexcept { return m_generators.Size(); }

        /// The swap-in signal: a thumbnail for this Guid became available (MAIN thread).
        Function<void(const Guid&)> OnThumbnailReady;

        ~ThumbnailService() { Reset(); }

        void RegisterGenerator(UniquePtr<IThumbnailGenerator> generator)
        {
            m_generators.PushBack(Move(generator));
        }

        /// Project-open wiring: where the cache lives, how to resolve instances, the job lane,
        /// and the content-hash source. Any previous state is dropped.
        void Configure(StringView cacheDirectory, Function<content::Instance*(const Guid&)> resolve,
                       EditorJobService* jobs, Function<u64(const Guid&)> contentHash,
                       StringView sourcesRoot)
        {
            Reset();
            m_cacheDirectory = String(cacheDirectory);
            m_resolve = Move(resolve);
            m_jobs = jobs;
            m_contentHash = Move(contentHash);
            m_sources = MakeUnique<foundation::vfs::NativeFileSystem>(DefaultAllocator(),
                                                                      sourcesRoot);
        }

        /// Project-close: drop the RAM cache and detach. In-flight jobs complete harmlessly
        /// against their slots (the alive flag is cleared).
        void Reset()
        {
            for (InFlight& flight : m_inFlight)
            {
                flight.slot->serviceAlive = false;
            }
            m_inFlight.Clear();
            m_entries.Clear();
            m_cacheDirectory.Clear();
            m_resolve = {};
            m_contentHash = {};
            m_jobs = nullptr;
            m_sources = {};
        }

        /// The resolved thumbnail, or EMPTY when none exists yet (the caller keeps its type
        /// icon). A miss schedules load-or-generate on the light lane; OnThumbnailReady fires
        /// when the swap-in is ready. Main thread only.
        [[nodiscard]] RefPtr<ui::Drawable> Get(const Guid& id)
        {
            if (id.IsNil() || m_jobs == nullptr || !m_resolve)
            {
                return {};
            }
            if (const Entry* entry = m_entries.Find(id))
            {
                return RefPtr<ui::Drawable>(entry->drawable.Get());
            }
            ScheduleLoad(id);
            return {};
        }

        /// Drop a cached thumbnail (cook/import invalidation); the next Get regenerates. The
        /// content-hash filenames make even a missed invalidation self-healing.
        void Invalidate(const Guid& id) { m_entries.Remove(id); }

        /// Drop the whole RAM cache (cook finished: recipe hashes moved). Unchanged assets
        /// reload from their unchanged disk files - cheap; changed ones regenerate.
        void InvalidateAll() { m_entries.Clear(); }

        [[nodiscard]] usize CachedCount() const noexcept { return m_entries.Size(); }

    private:
        struct Entry
        {
            RefPtr<OwnedThumbnailDrawable> drawable;
        };

        // The heap slot both closures own (editor-jobs.md lifetime rule): the service dtor /
        // Reset clears serviceAlive; the completion always deletes the slot. All flags are
        // main-thread; the worker only touches `pixels` + `ok`.
        struct JobSlot
        {
            Guid id{};
            bool serviceAlive = true;      // main-thread flag (service dtor/Reset clears)
            bool ok = false;               // worker -> completion result
            bool negative = false;         // completion: cache "no thumbnail" (stop rescheduling)
            bool staleDiskFile = false;    // disk cache failed to load AND no payload to
                                           // regenerate from (Prepare was skipped because the
                                           // file existed): delete the file, retry fully
            image::Image pixels;           // worker output
            Array<byte> payload;           // main-thread Prepare output, consumed by the worker
            String diskPath;               // empty = RAM-only (unknown content hash)
            IThumbnailGenerator* generator = nullptr; // borrowed; generators live on the service
        };
        struct InFlight
        {
            Guid id{};
            JobSlot* slot = nullptr;
        };

        [[nodiscard]] IThumbnailGenerator* GeneratorFor(StringView typeName)
        {
            for (const UniquePtr<IThumbnailGenerator>& generator : m_generators)
            {
                for (StringView covered : generator->AssetTypeNames())
                {
                    if (covered == typeName)
                    {
                        return generator.Get();
                    }
                }
            }
            return nullptr;
        }

        [[nodiscard]] String CachePathFor(const Guid& id, u64 hash) const
        {
            return Format(u8"{}/{}-{}.png", m_cacheDirectory, id, hash);
        }

        void ScheduleLoad(const Guid& id);
        void CompleteLoad(JobSlot* slot);

        String m_cacheDirectory;
        UniquePtr<foundation::vfs::NativeFileSystem> m_sources; // the project Sources/ mount
        Function<content::Instance*(const Guid&)> m_resolve;
        Function<u64(const Guid&)> m_contentHash;
        EditorJobService* m_jobs = nullptr;
        Array<UniquePtr<IThumbnailGenerator>> m_generators;
        HashMap<Guid, Entry> m_entries;
        Array<InFlight> m_inFlight;
    };
}
