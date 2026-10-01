// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene - :pie_page_interface partition (agent-playtesting-and-asset-creation.md P3).
//
// IPieInstancePage: what one play-in-editor instance, a Game tab, lets the PIE tools act through:
// its id, its run's start and stop, the state of the run, and a capture of what it renders. The
// Game page publishes it (EditorPage::Service<IPieInstancePage>).
module;
#include "Core/Prelude.h"

export module editor.scene:pie_page_interface;

import foundation.core;
import editor.core;
import :scene_page_interface; // ViewportCapture

using namespace foundation::core;

export namespace editor
{
    /// Where a PIE instance's startup script stands.
    enum class PieScriptState : u8
    {
        None,    ///< the project has no startup script, or the run has not started one
        Running,
        Faulted, ///< it stopped on its own (IPieInstancePage::ScriptFault says why)
    };

    class IPieInstancePage : public IPageService
    {
    public:
        /// The tab's id: `game-page` for the primary, `game-page-1`, `game-page-2`, ... for the
        /// instances Play New Instance opens. The dock persists the tab under it too.
        [[nodiscard]] virtual StringView PieId() const noexcept = 0;

        /// Asks for a cook and starts the run once the cook is idle; nothing while running.
        virtual void Play() = 0;
        /// Stops the run; the tab stays open.
        virtual void Stop() = 0;

        /// A run is going.
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
        /// Play was asked for and the run waits on the cook.
        [[nodiscard]] virtual bool IsStarting() const noexcept = 0;
        /// The scene the run is in, empty when it has none (a script that owns boot, between
        /// levels).
        [[nodiscard]] virtual StringView SceneName() const noexcept = 0;
        /// Gameplay seconds since the run started, the clock the game script moves by.
        [[nodiscard]] virtual f64 GameTime() const noexcept = 0;
        /// Frames rendered since the run started.
        [[nodiscard]] virtual u64 FrameCount() const noexcept = 0;
        /// The startup script's state, and the reason a faulted one stopped.
        [[nodiscard]] virtual PieScriptState ScriptState() const noexcept = 0;
        [[nodiscard]] virtual StringView ScriptFault() const noexcept = 0;

        /// Asks for the viewport's next rendered frame, after the game's own UI and overlays, as
        /// a PNG at `path` (its directory must exist), replacing a pending request.
        virtual void RequestViewportCapture(StringView path) = 0;
        /// The latest request's state, as it advances frame by frame.
        [[nodiscard]] virtual const ViewportCapture& LastViewportCapture() const noexcept = 0;

    protected:
        ~IPieInstancePage() = default;
    };
}
