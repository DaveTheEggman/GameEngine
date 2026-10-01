// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene - :pie_page_interface partition (agent-playtesting-and-asset-creation.md P3).
//
// IPieInstancePage: what one play-in-editor instance, a Game tab, lets the PIE tools act through:
// its id, its run's start and stop, the state of the run, a capture of what it renders, the scene
// and game script it runs, and a scripted input timeline in place of the viewport's (P4). The
// Game page publishes it (EditorPage::Service<IPieInstancePage>).
module;
#include "Core/Prelude.h"

export module editor.scene:pie_page_interface;

import foundation.core;
import foundation.input;
import foundation.scene;
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
        /// Seconds of frames since the run started, unscaled: it keeps going while the game has
        /// its scene paused at time scale 0 (behind a menu, which still works); it stands still
        /// while the debugger holds the run. Scripted input is timed by it.
        [[nodiscard]] virtual f64 RunTime() const noexcept = 0;
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

        /// The scene the run is in now; null when it has none.
        [[nodiscard]] virtual foundation::scene::Scene* RunningScene() noexcept = 0;
        /// Reads a property of the running game script; NotFound without a script or with no
        /// such property.
        [[nodiscard]] virtual Result<Variant> GetScriptProperty(StringView name) const = 0;

        /// Plays `source` into this instance in place of the viewport's input, its time zero
        /// now: the page advances it each frame by the run's time. A script already playing is
        /// dropped.
        virtual void BeginScriptedInput(UniquePtr<foundation::input::ScriptedInputSource> source) = 0;
        /// Ends the script: one frame letting go of everything it holds, then the viewport's
        /// input returns. A stop ends it at once.
        virtual void EndScriptedInput() = 0;
        /// A script is installed (until the viewport's input is back).
        [[nodiscard]] virtual bool IsScripted() const noexcept = 0;

    protected:
        ~IPieInstancePage() = default;
    };
}
