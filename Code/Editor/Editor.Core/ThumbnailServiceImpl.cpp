// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor Core - ThumbnailService implementation.
//
// ScheduleLoad gathers everything on the MAIN thread (instance resolve, generator lookup, the
// Prepare payload, the content-hash cache path), the light worker does disk-load-or-generate +
// PNG write, and CompleteLoad publishes the drawable + fires OnThumbnailReady back on the main
// thread. Failures cache a NEGATIVE entry so per-frame Get calls never re-schedule a known
// miss - Invalidate clears it. Teardown ordering: the app Shutdowns the job service (joins the
// light worker) BEFORE destroying this service - the worker borrows the generator.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.core;

import foundation.core;
import foundation.image.io;

namespace editor
{
    using namespace foundation::core;
    namespace image = foundation::image;

    void ThumbnailService::ScheduleLoad(const Guid& id)
    {
        if (m_inFlight.Size() >= kMaxInFlight)
        {
            return; // budget: the next Get retries once the lane drains
        }
        for (const InFlight& flight : m_inFlight)
        {
            if (flight.id == id)
            {
                return; // already on its way
            }
        }
        content::Instance* instance = m_resolve(id);
        if (instance == nullptr)
        {
            m_entries.InsertOrAssign(id, Entry{}); // unknown id: negative (Invalidate clears)
            return;
        }
        IThumbnailGenerator* generator = GeneratorFor(instance->TypeName());
        if (generator == nullptr)
        {
            m_entries.InsertOrAssign(id, Entry{}); // no generator: the type icon stays
            return;
        }

        auto* slot = DefaultAllocator().New<JobSlot>();
        slot->id = id;
        slot->generator = generator;
        // Unknown content hash (mid-cook, no record yet) = RAM-only: never write a cache file
        // that can never match again. The cook-finished invalidation regenerates with the real
        // hash and THEN persists.
        const u64 hash = m_contentHash ? m_contentHash(id) : 0;
        if (hash != 0)
        {
            slot->diskPath = CachePathFor(id, hash);
        }

        // MAIN-thread Prepare: the worker never touches the content DB. A Prepare failure is a
        // negative entry (bad/missing source stream) - logged once here, not every frame.
        if (slot->diskPath.IsEmpty() || !FileExists(slot->diskPath.AsView()))
        {
            const Status prepared = generator->Prepare(*instance, *m_sources, slot->payload);
            if (!prepared.IsOk())
            {
                LOG_WARNING(u8"Thumbnails", u8"prepare failed for '{}' ({}): error {}",
                            instance->Name(), instance->TypeName(),
                            static_cast<u32>(prepared.Code()));
                m_entries.InsertOrAssign(id, Entry{});
                DefaultAllocator().Delete(slot);
                return;
            }
        }

        m_inFlight.PushBack(InFlight{id, slot});
        ThumbnailService* self = this;
        m_jobs->SubmitLight(
            Function<void()>{[slot]()
                             {
                                 // LIGHT worker: disk cache first, else generate + persist.
                                 if (!slot->diskPath.IsEmpty() &&
                                     image::io::LoadImage(slot->diskPath.AsView(), slot->pixels)
                                         .IsOk())
                                 {
                                     slot->ok = true;
                                     return;
                                 }
                                 if (slot->payload.IsEmpty() && !slot->diskPath.IsEmpty())
                                 {
                                     // The cache file existed at schedule time (so Prepare was
                                     // skipped) but would not load - a stale/corrupt write.
                                     // Self-heal: delete it and retry the FULL path next Get.
                                     slot->staleDiskFile = true;
                                     return;
                                 }
                                 const Status generated =
                                     slot->generator->Generate(Span<const byte>(slot->payload.Data(),
                                                                                slot->payload.Size()),
                                                               slot->pixels);
                                 if (!generated.IsOk())
                                 {
                                     slot->negative = true;
                                     return;
                                 }
                                 slot->ok = true;
                                 if (!slot->diskPath.IsEmpty())
                                 {
                                     (void)image::io::SaveImage(slot->pixels,
                                                                slot->diskPath.AsView(),
                                                                image::io::ImageFileFormat::PNG);
                                 }
                             }},
            Function<void()>{[self, slot]() { self->CompleteLoad(slot); }});
    }

    void ThumbnailService::CompleteLoad(JobSlot* slot)
    {
        // Main thread. The service may have been Reset (project closed) mid-flight; the slot's
        // flag is the guard and the slot is always deleted here.
        if (slot->serviceAlive)
        {
            for (usize i = 0; i < m_inFlight.Size(); ++i)
            {
                if (m_inFlight[i].slot == slot)
                {
                    m_inFlight.RemoveAt(i);
                    break;
                }
            }
            if (slot->ok)
            {
                Entry entry;
                entry.drawable = MakeRef<OwnedThumbnailDrawable>(DefaultAllocator(),
                                                                 Move(slot->pixels));
                m_entries.InsertOrAssign(slot->id, Move(entry));
                if (OnThumbnailReady)
                {
                    OnThumbnailReady(slot->id);
                }
            }
            else if (slot->staleDiskFile)
            {
                // Delete the unloadable cache file and cache NOTHING - the next Get takes
                // the full Prepare + Generate path and overwrites it.
                LOG_WARNING(u8"Thumbnails", u8"stale cache file replaced for {}", slot->id);
                (void)FileDelete(slot->diskPath.AsView());
            }
            else
            {
                // Warn on generate failure: the negative cache would otherwise hide it.
                LOG_WARNING(u8"Thumbnails", u8"generate failed for {} (cached negative)",
                            slot->id);
                m_entries.InsertOrAssign(slot->id, Entry{}); // negative: stop rescheduling
            }
        }
        DefaultAllocator().Delete(slot);
    }
}
