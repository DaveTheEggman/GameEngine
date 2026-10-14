// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::DefaultApp - the `engine.defaultapp:profiler_overlay` partition: an application's
// profiler overlay (Specs/profiler.md, P4). Whether an app has one, and what turns it on, is the
// app's to decide; the UI only gives it a place to draw, its screen tier's global overlay layer
// (PushScreenOverlay), above every scene and run, where a view that takes no pointer leaves the
// game playable under it. DefaultApplication owns one (F3, --profile-overlay, the web page's
// ?profile-overlay); another app, the editor's Game tab say, can own its own.
module;
#include "Core/Prelude.h"
#include "Profiler/Profiler.h"

export module engine.defaultapp:profiler_overlay;

import foundation.core;
import foundation.profiler;
import foundation.profiler.ui;
import foundation.ui;
import engine.ui;

using namespace foundation::core;

export namespace engine::runtime
{
    class ProfilerOverlayHost
    {
    public:
        /// `profiler` is the store it shows and arms while shown (a test's own, or the process's).
        explicit ProfilerOverlayHost(IAllocator& allocator,
                                     foundation::profiler::Profiler& profiler = foundation::profiler::Profiler::Get())
            : m_allocator(&allocator), m_profiler(&profiler)
        {
        }
        ~ProfilerOverlayHost() { Hide(); }
        ProfilerOverlayHost(const ProfilerOverlayHost&) = delete;
        ProfilerOverlayHost& operator=(const ProfilerOverlayHost&) = delete;

        /// Show it on `ui`'s screen tier and arm the profiler (counted: a capture or the panel
        /// keeps its own arm). Nothing in a build without the profiler (Shipping).
        void Show(engine::ui::UISubsystem& ui)
        {
#if BUILD_PROFILING
            if (m_ui != nullptr)
            {
                return;
            }
            if (m_overlay.Get() == nullptr)
            {
                m_source = MakeUnique<foundation::profiler::LiveProfileSource>(*m_allocator, *m_profiler);
                m_overlay = MakeRef<foundation::profiler::ui::ProfilerOverlay>(*m_allocator, *m_allocator);
                m_overlay->SetSource(m_source.Get());
                foundation::ui::LayoutStyle fill;
                fill.Width = foundation::ui::SizeSpec::Match();
                fill.Height = foundation::ui::SizeSpec::Match();
                m_overlay->SetLayout(fill);
            }
            ui.PushScreenOverlay(RefPtr<foundation::ui::View>(m_overlay.Get()));
            m_profiler->Arm();
            m_ui = &ui;
            m_overlay->Refresh();
#else
            (void)ui;
#endif
        }

        /// Take it down and let the profiler go. Before the UI it shows on shuts down.
        void Hide()
        {
            if (m_ui == nullptr)
            {
                return;
            }
            m_ui->RemoveScreenOverlay(m_overlay.Get());
            m_profiler->Disarm();
            m_ui = nullptr;
        }

        void Toggle(engine::ui::UISubsystem& ui)
        {
            if (Shown())
            {
                Hide();
            }
            else
            {
                Show(ui);
            }
        }

        [[nodiscard]] bool Shown() const noexcept { return m_ui != nullptr; }

        /// Each frame, before the UI draws: the views take the frames that ended since.
        void Refresh()
        {
            if (m_ui != nullptr)
            {
                m_overlay->Refresh();
            }
        }

        /// The overlay once it has been shown (tests, an app placing it in another corner).
        [[nodiscard]] foundation::profiler::ui::ProfilerOverlay* Overlay() noexcept { return m_overlay.Get(); }

    private:
        IAllocator* m_allocator;
        foundation::profiler::Profiler* m_profiler;
        engine::ui::UISubsystem* m_ui = nullptr; // while shown
        UniquePtr<foundation::profiler::LiveProfileSource> m_source;
        RefPtr<foundation::profiler::ui::ProfilerOverlay> m_overlay;
    };
}
