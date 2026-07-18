// InputActions - the draconic.input P1 consumer proof: a windowed app driving a "player"
// square via NAMED ACTIONS, never raw keys. One hand-authored InputMap wires Move to
// WASD (composite, smoothed) AND the left stick, Jump to Space / gamepad south, and a
// Menu set (Escape toggles it EXCLUSIVE) that demonstrates suppression + held-latching:
// while the "menu" is open the square stops responding, a held key never re-fires on
// close, and Confirm (Space) belongs to the menu context. The square renders as the
// window clear color shifting with position - deliberately minimal; the point is the
// ACTION layer, not the drawing.

#include "Core/Prelude.h"
#include "Runtime/Client/AppMain.h"

import draconic.core;
import draconic.runtime;
import draconic.runtime.client;
import draconic.shell;
import draconic.runtime.desktop;
import draconic.shell.desktop;
import draconic.graphics;
import draconic.graphics.gpu;
import draconic.input;
import draconic.input.subsystem;

namespace core = draconic::core;
namespace runtime = draconic::runtime;
namespace graphics = draconic::graphics;
namespace shell = draconic::shell;
namespace input = draconic::input;

namespace
{
    [[nodiscard]] input::InputMap MakeMap()
    {
        input::InputMap map;

        input::ActionSet gameplay;
        gameplay.name = core::String(u8"Gameplay");
        {
            input::Action move;
            move.name = core::String(u8"Move");
            move.kind = input::ActionKind::Axis2D;
            input::Binding wasd;
            wasd.source = input::BindingSource::Composite2D;
            wasd.negX = static_cast<core::u32>(shell::KeyCode::A);
            wasd.posX = static_cast<core::u32>(shell::KeyCode::D);
            wasd.negY = static_cast<core::u32>(shell::KeyCode::S);
            wasd.posY = static_cast<core::u32>(shell::KeyCode::W);
            move.bindings.PushBack(wasd);
            input::Binding stick;
            stick.source = input::BindingSource::GamepadStick;
            stick.code = static_cast<core::u32>(input::StickCode::Left);
            stick.invert = true;   // stick +Y is down; the square's +Y is up
            move.bindings.PushBack(stick);
            move.processors.sensitivity = 6.0f;   // keyboard ramps like an analog stick
            move.processors.gravity = 10.0f;
            move.processors.snap = true;
            gameplay.actions.PushBack(static_cast<input::Action&&>(move));
        }
        {
            input::Action jump;
            jump.name = core::String(u8"Jump");
            jump.kind = input::ActionKind::Button;
            input::Binding space;
            space.source = input::BindingSource::Key;
            space.code = static_cast<core::u32>(shell::KeyCode::Space);
            jump.bindings.PushBack(space);
            input::Binding pad;
            pad.source = input::BindingSource::GamepadButton;
            pad.code = 0;
            jump.bindings.PushBack(pad);
            gameplay.actions.PushBack(static_cast<input::Action&&>(jump));
        }
        map.sets.PushBack(static_cast<input::ActionSet&&>(gameplay));

        input::ActionSet menu;
        menu.name = core::String(u8"Menu");
        menu.priority = 10;
        {
            input::Action confirm;
            confirm.name = core::String(u8"Confirm");
            confirm.kind = input::ActionKind::Button;
            input::Binding space;
            space.source = input::BindingSource::Key;
            space.code = static_cast<core::u32>(shell::KeyCode::Space);
            confirm.bindings.PushBack(space);
            menu.actions.PushBack(static_cast<input::Action&&>(confirm));
        }
        map.sets.PushBack(static_cast<input::ActionSet&&>(menu));
        return map;
    }

    class InputActionsApp final : public runtime::IApplication
    {
    public:
        void Configure(runtime::IApplicationHost& host) override
        {
            m_input = host.Ctx().AddSubsystem<input::InputSubsystem>(
                host.Shell() != nullptr ? host.Shell()->Input() : nullptr);
        }

        void OnStartup(runtime::IApplicationHost&) override
        {
            m_input->SetMap(MakeMap());
            m_move = m_input->Runtime().Resolve(u8"Move");
            m_jump = m_input->Runtime().Resolve(u8"Jump");
            m_confirm = m_input->Runtime().Resolve(u8"Confirm");
            core::ConsoleWrite(u8"InputActions: WASD/left-stick moves (watch the clear color),\n"
                               u8"  Space/pad-south = Jump, Escape toggles the exclusive Menu set\n"
                               u8"  (movement freezes; Space becomes Confirm; a held key never\n"
                               u8"  re-fires across the menu boundary).\n");
        }

        void OnUpdate(runtime::IApplicationHost& host, core::f32 dt) override
        {
            input::ActionRuntime& actions = m_input->Runtime();

            // Escape toggles the modal set - raw poll for the TOGGLE only (it must work in
            // both contexts; a real game would bind it in a always-on "System" set).
            auto* shellInput = host.Shell() != nullptr ? host.Shell()->Input() : nullptr;
            auto* keyboard = shellInput != nullptr ? shellInput->Keyboard() : nullptr;
            if (keyboard != nullptr && keyboard->IsKeyPressed(shell::KeyCode::Escape))
            {
                if (actions.ExclusiveDepth() == 0)
                {
                    actions.PushExclusiveSet(u8"Menu");
                    core::ConsoleWrite(u8"InputActions: MENU open (gameplay suppressed)\n");
                }
                else
                {
                    actions.PopExclusiveSet();
                    core::ConsoleWrite(u8"InputActions: menu closed\n");
                }
            }

            const core::Float2 move = actions.Value2D(m_move);
            m_x = core::Clamp(m_x + move.x * dt * 0.6f, 0.0f, 1.0f);
            m_y = core::Clamp(m_y + move.y * dt * 0.6f, 0.0f, 1.0f);
            if (actions.WasPressed(m_jump)) { core::ConsoleWrite(u8"InputActions: Jump!\n"); }
            if (actions.WasPressed(m_confirm)) { core::ConsoleWrite(u8"InputActions: Confirm.\n"); }
        }

        void OnRenderWindow(runtime::IApplicationHost&, graphics::FrameContext& frame) override
        {
            // Position IS the color: x = red, y = green - visible action-driven state
            // without dragging in the renderer.
            frame.Clear(0.1f + 0.8f * m_x, 0.1f + 0.8f * m_y, 0.25f, 1.0f);
        }

    private:
        input::InputSubsystem* m_input = nullptr;
        input::ActionRef m_move;
        input::ActionRef m_jump;
        input::ActionRef m_confirm;
        core::f32 m_x = 0.5f;
        core::f32 m_y = 0.5f;
    };
}

DRACONIC_APP_MAIN(InputActionsApp)
