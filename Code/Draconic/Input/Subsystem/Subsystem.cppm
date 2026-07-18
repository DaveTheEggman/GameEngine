// Draconic::InputSubsystem - the `draconic.input.subsystem` module.
//
// The runtime hookup: owns the ActionRuntime + the device provider and evaluates once per
// frame in Update (before scenes tick - Subsystem registration order puts input ahead of
// the scene subsystem in every assembly that adds it first). The provider seam is the
// play-in-editor story: the player passes the shell's devices, the editor's Game tab will
// pass its viewport's gated InputSurface facades.

module;
#include "Core/Prelude.h"

export module draconic.input.subsystem;

import draconic.core;
import draconic.shell;
import draconic.runtime;
import draconic.input;

using namespace draconic::core;

export namespace draconic::input
{
    class InputSubsystem final : public draconic::runtime::Subsystem
    {
    public:
        /// `input` = the shell's device hub (null tolerated: headless runs read released).
        explicit InputSubsystem(draconic::shell::IInputManager* input)
            : m_shellSource(input) {}

        [[nodiscard]] ActionRuntime& Runtime() noexcept { return m_runtime; }

        /// Installs (copies) a map - from the cooked resource, a test, or hand-authored.
        void SetMap(const InputMap& map) { m_runtime.SetMap(map); }

        /// Overrides the device source (play-in-editor: the Game viewport's gated facades).
        /// Null restores the shell devices.
        void SetSourceProvider(IInputSourceProvider* provider) noexcept { m_override = provider; }

        void Update(f32 deltaTime) override
        {
            IInputSourceProvider& devices =
                (m_override != nullptr) ? *m_override
                                        : static_cast<IInputSourceProvider&>(m_shellSource);
            m_runtime.Update(devices, deltaTime);
        }

    private:
        ShellInputSource m_shellSource;
        IInputSourceProvider* m_override = nullptr;   // borrowed
        ActionRuntime m_runtime;
    };
}
