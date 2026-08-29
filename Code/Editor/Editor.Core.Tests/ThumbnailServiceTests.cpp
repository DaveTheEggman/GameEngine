// ThumbnailService tests. A stub generator + a real EditorJobService
// (Update pumped like JobServiceTests) + a scratch cache dir exercise the full pipeline
// headlessly: icon-fallback-until-ready (Get empty, then a drawable + ONE ready signal),
// negative caching (unknown id / unsupported type / failed generate never re-schedule),
// content-hash disk round-trip (a second service instance serves from disk without invoking
// the generator), and RAM-only mode while the content hash is unknown.
//
// The stub's resolve returns a pointer to a NEVER-DEREFERENCED sentinel instance for ids the
// test declares "known": the service only touches the instance through TypeName()/Name(), so
// the stub builds one real Instance over a scratch ContentDatabase-free constructor.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.content;
import foundation.image;
import foundation.ui;
import editor.core;
import foundation.vfs;

using namespace foundation::core;
using namespace editor;
namespace content = foundation::content;
namespace image = foundation::image;
namespace ui = foundation::ui;

namespace
{
    // Pump the light lane to completion (bounded so a hang fails, not spins).
    void PumpLight(EditorJobService& jobs)
    {
        usize guard = 0;
        while (jobs.IsLightBusy() && guard++ < 2'000'000)
        {
            jobs.Update();
        }
        jobs.Update(); // drain the completed queue
    }

    class StubGenerator final : public IThumbnailGenerator
    {
    public:
        int* prepareCount = nullptr;
        int* generateCount = nullptr;
        bool failGenerate = false;

        [[nodiscard]] Span<const StringView> AssetTypeNames() const override
        {
            static constexpr StringView kTypes[] = {u8"StubAsset"};
            return Span<const StringView>(kTypes, 1);
        }
        [[nodiscard]] Status Prepare(content::Instance&, foundation::vfs::IFileSystem&,
                                     Array<byte>& payload) override
        {
            if (prepareCount != nullptr)
            {
                ++*prepareCount;
            }
            payload.PushBack(byte{42});
            return Status{};
        }
        [[nodiscard]] Status Generate(Span<const byte>, image::Image& out) override
        {
            if (generateCount != nullptr)
            {
                ++*generateCount;
            }
            if (failGenerate)
            {
                return Status{ErrorCode::Internal};
            }
            out = image::Image(8, 8, image::PixelFormat::RGBA8);
            Span<u8> px = out.PixelDataMut();
            for (usize i = 0; i < px.Size(); i += 4)
            {
                px[i + 0] = 200;
                px[i + 3] = 255;
            }
            return Status{};
        }
    };

    struct Fixture
    {
        content::ContentDatabase db;
        content::Instance instance;
        EditorJobService jobs;
        ThumbnailService service;
        Guid known{0x1111, 0x2222};
        int prepares = 0;
        int generates = 0;

        explicit Fixture(StringView cacheDir, u64 hash = 0x77, bool failGenerate = false)
            : db(NullMount(), nullptr, u8"asset"),
              instance(db, *db.RootGroup(), Guid{0x1111, 0x2222}, u8"Stub", u8"tests",
                       u8"StubAsset")
        {
            (void)CreateDirectory(cacheDir);
            auto generator = MakeUnique<StubGenerator>(DefaultAllocator());
            generator->prepareCount = &prepares;
            generator->generateCount = &generates;
            generator->failGenerate = failGenerate;
            service.RegisterGenerator(Move(generator));
            content::Instance* inst = &instance;
            Guid knownId = known;
            service.Configure(
                cacheDir,
                Function<content::Instance*(const Guid&)>{
                    [inst, knownId](const Guid& id)
                    { return id == knownId ? inst : nullptr; }},
                &jobs, Function<u64(const Guid&)>{[hash](const Guid&) { return hash; }},
                u8"thumbs_empty_mount");
        }

        // An EMPTY dedicated mount: ContentDatabase's ctor SCANS the mount, and a null
        // serializer factory only stays un-invoked if the scan finds no envelopes.
        static foundation::vfs::NativeFileSystem& NullMount()
        {
            (void)CreateDirectory(u8"thumbs_empty_mount");
            static foundation::vfs::NativeFileSystem fs{StringView(u8"thumbs_empty_mount")};
            return fs;
        }
    };
}

TEST_CASE("thumbnails: icon-first, then the drawable + ONE ready signal")
{
    // Scratch persists across runs - a stale cache file would serve and skip Generate.
    (void)RemoveDirectoryRecursive(u8"thumbs_happy");
    Fixture f(u8"thumbs_happy");
    int ready = 0;
    f.service.OnThumbnailReady = [&ready](const Guid&) { ++ready; };

    CHECK(!f.service.Get(f.known)); // miss schedules; caller keeps its icon
    PumpLight(f.jobs);
    RefPtr<ui::Drawable> thumb = f.service.Get(f.known);
    CHECK(thumb);
    CHECK(ready == 1);
    CHECK(f.generates == 1);
    CHECK(f.service.Get(f.known).Get() == thumb.Get()); // stable instance, no rescheduling
}

TEST_CASE("thumbnails: negatives never re-schedule")
{
    (void)RemoveDirectoryRecursive(u8"thumbs_negative");
    (void)RemoveDirectoryRecursive(u8"thumbs_fail");
    Fixture f(u8"thumbs_negative");
    // Unknown id: negative entry, no job.
    CHECK(!f.service.Get(Guid{0x9, 0x9}));
    CHECK(!f.service.Get(Guid{0x9, 0x9}));
    CHECK(f.service.CachedCount() == 1);
    CHECK(f.prepares == 0);

    // Failed generate: negative after the pump; no retry without Invalidate.
    Fixture g(u8"thumbs_fail", 0x77, /*failGenerate=*/true);
    CHECK(!g.service.Get(g.known));
    PumpLight(g.jobs);
    CHECK(!g.service.Get(g.known));
    CHECK(g.generates == 1);
    PumpLight(g.jobs);
    CHECK(g.generates == 1); // negative held

    // Invalidate re-arms.
    g.service.Invalidate(g.known);
    CHECK(!g.service.Get(g.known));
    PumpLight(g.jobs);
    CHECK(g.generates == 2);
}

TEST_CASE("thumbnails: content-hash disk cache round-trips without regenerating")
{
    (void)RemoveDirectoryRecursive(u8"thumbs_disk");
    const Guid id{0x1111, 0x2222};
    {
        Fixture f(u8"thumbs_disk", 0xabc);
        (void)f.service.Get(id);
        PumpLight(f.jobs);
        CHECK(f.service.Get(id));
        CHECK(f.generates == 1);
    }
    {
        // A fresh service (new session): the <guid>-<hash>.png file serves; Generate is NOT
        // called (Prepare is also skipped - the disk file short-circuits it).
        Fixture f(u8"thumbs_disk", 0xabc);
        (void)f.service.Get(id);
        PumpLight(f.jobs);
        CHECK(f.service.Get(id));
        CHECK(f.generates == 0);
        CHECK(f.prepares == 0);
    }
    {
        // A DIFFERENT hash (content changed): the old file mismatches; regenerate.
        Fixture f(u8"thumbs_disk", 0xdef);
        (void)f.service.Get(id);
        PumpLight(f.jobs);
        CHECK(f.service.Get(id));
        CHECK(f.generates == 1);
    }
}

TEST_CASE("thumbnails: unknown content hash = RAM-only (no cache file to mismatch later)")
{
    (void)RemoveDirectoryRecursive(u8"thumbs_ram");
    const Guid id{0x1111, 0x2222};
    {
        Fixture f(u8"thumbs_ram", 0);
        (void)f.service.Get(id);
        PumpLight(f.jobs);
        CHECK(f.service.Get(id));
        CHECK(f.generates == 1);
    }
    {
        // Nothing persisted: a fresh session regenerates.
        Fixture f(u8"thumbs_ram", 0);
        (void)f.service.Get(id);
        PumpLight(f.jobs);
        CHECK(f.generates == 1);
    }
}

TEST_CASE("thumbnails: a corrupt cache file self-heals (deleted + regenerated, never a negative)")
{
    (void)RemoveDirectoryRecursive(u8"thumbs_corrupt");
    const Guid id{0x1111, 0x2222};
    String path;
    {
        Fixture f(u8"thumbs_corrupt", 0x55);
        (void)f.service.Get(id);
        PumpLight(f.jobs);
        CHECK(f.service.Get(id));
        path = Format(u8"{}/{}-{}.png", StringView(u8"thumbs_corrupt"), id, u64{0x55});
        CHECK(FileExists(path.AsView()));
    }
    // Corrupt the cache file: the load fails, and because the file existed Prepare was
    // skipped - the old code cached a silent PERMANENT negative here.
    (void)WriteFile(path.AsView(), Span<const byte>(reinterpret_cast<const byte*>("junk"), 4));
    {
        Fixture f(u8"thumbs_corrupt", 0x55);
        CHECK(!f.service.Get(id)); // schedules; file exists so Prepare skipped
        PumpLight(f.jobs);
        // Self-heal pass: the stale file was deleted and NOTHING was cached...
        CHECK(!FileExists(path.AsView()));
        CHECK(f.service.CachedCount() == 0);
        // ...so the next Get takes the full Prepare + Generate path and succeeds.
        CHECK(!f.service.Get(id));
        PumpLight(f.jobs);
        CHECK(f.service.Get(id));
        CHECK(f.generates == 1);
        CHECK(FileExists(path.AsView())); // rewritten
    }
}
