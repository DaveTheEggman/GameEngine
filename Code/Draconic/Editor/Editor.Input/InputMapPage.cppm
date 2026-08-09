// Draconic::EditorInput - the `editor.input` module.
//
// InputMapPage (input P2): the editing surface for InputMapAsset - a scrollable
// sets > actions > bindings outline with add/remove, in-place renames, kind/interaction
// cycling, priority nudges, and "Listen" rebind capture (CaptureBinding polled per frame,
// filtered by the action's kind; Esc cancels). Every mutation is one UNDOABLE command via
// whole-map snapshots (the map is small data - the generic SnapshotCommand idea, page-local).
// Save validates first: a kind-mismatched map never reaches the cook.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Core/Log/Log.h"
#include <cstdlib>

export module editor.input;

import foundation.core;
import foundation.content;
import foundation.shell;
import foundation.runtime;
import foundation.runtime.client;
import foundation.input;
import input.pipeline;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.ui.runtime;
import editor.core;
import editor.app;

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace input = foundation::input;

    namespace detail
    {
        [[nodiscard]] inline String KeyName(u32 code)
        {
            const foundation::shell::KeyCode key = static_cast<foundation::shell::KeyCode>(code);
            const u32 a = static_cast<u32>(foundation::shell::KeyCode::A);
            const u32 z = static_cast<u32>(foundation::shell::KeyCode::Z);
            const u32 n0 = static_cast<u32>(foundation::shell::KeyCode::Num0);
            const u32 n9 = static_cast<u32>(foundation::shell::KeyCode::Num9);
            const u32 f1 = static_cast<u32>(foundation::shell::KeyCode::F1);
            const u32 f24 = static_cast<u32>(foundation::shell::KeyCode::F24);
            String out;
            if (code >= a && code <= z)
            {
                out.PushBack(static_cast<utf8char>(u8'A' + (code - a)));
                return out;
            }
            if (code >= n0 && code <= n9)
            {
                out.PushBack(static_cast<utf8char>(u8'0' + (code - n0)));
                return out;
            }
            if (code >= f1 && code <= f24)
            {
                out.Append(u8"F");
                AppendValue(out, static_cast<u64>(code - f1 + 1));
                return out;
            }
            switch (key)
            {
            case foundation::shell::KeyCode::Return:
                return String(u8"Return");
            case foundation::shell::KeyCode::Escape:
                return String(u8"Escape");
            case foundation::shell::KeyCode::Backspace:
                return String(u8"Backspace");
            case foundation::shell::KeyCode::Tab:
                return String(u8"Tab");
            case foundation::shell::KeyCode::Space:
                return String(u8"Space");
            case foundation::shell::KeyCode::Left:
                return String(u8"Left");
            case foundation::shell::KeyCode::Right:
                return String(u8"Right");
            case foundation::shell::KeyCode::Up:
                return String(u8"Up");
            case foundation::shell::KeyCode::Down:
                return String(u8"Down");
            case foundation::shell::KeyCode::LeftShift:
                return String(u8"LShift");
            case foundation::shell::KeyCode::RightShift:
                return String(u8"RShift");
            case foundation::shell::KeyCode::LeftCtrl:
                return String(u8"LCtrl");
            case foundation::shell::KeyCode::RightCtrl:
                return String(u8"RCtrl");
            case foundation::shell::KeyCode::LeftAlt:
                return String(u8"LAlt");
            case foundation::shell::KeyCode::RightAlt:
                return String(u8"RAlt");
            default:
                break;
            }
            out.Append(u8"Key#");
            AppendValue(out, static_cast<u64>(code));
            return out;
        }

        [[nodiscard]] inline StringView PadButtonName(u32 code)
        {
            switch (static_cast<foundation::shell::GamepadButton>(code))
            {
            case foundation::shell::GamepadButton::South:
                return u8"Pad South";
            case foundation::shell::GamepadButton::East:
                return u8"Pad East";
            case foundation::shell::GamepadButton::West:
                return u8"Pad West";
            case foundation::shell::GamepadButton::North:
                return u8"Pad North";
            case foundation::shell::GamepadButton::LeftShoulder:
                return u8"Pad LB";
            case foundation::shell::GamepadButton::RightShoulder:
                return u8"Pad RB";
            case foundation::shell::GamepadButton::DPadUp:
                return u8"DPad Up";
            case foundation::shell::GamepadButton::DPadDown:
                return u8"DPad Down";
            case foundation::shell::GamepadButton::DPadLeft:
                return u8"DPad Left";
            case foundation::shell::GamepadButton::DPadRight:
                return u8"DPad Right";
            case foundation::shell::GamepadButton::Start:
                return u8"Pad Start";
            case foundation::shell::GamepadButton::Back:
                return u8"Pad Back";
            default:
                return u8"Pad Button";
            }
        }

        [[nodiscard]] inline String DescribeBinding(const input::Binding& b)
        {
            switch (b.source)
            {
            case input::BindingSource::Key:
                return KeyName(b.code);
            case input::BindingSource::MouseButton:
                switch (static_cast<foundation::shell::MouseButton>(b.code))
                {
                case foundation::shell::MouseButton::Left:
                    return String(u8"Mouse Left");
                case foundation::shell::MouseButton::Right:
                    return String(u8"Mouse Right");
                case foundation::shell::MouseButton::Middle:
                    return String(u8"Mouse Middle");
                default:
                    return String(u8"Mouse Button");
                }
            case input::BindingSource::MouseAxis:
                switch (static_cast<input::MouseAxisCode>(b.code))
                {
                case input::MouseAxisCode::DeltaX:
                    return String(u8"Mouse dX");
                case input::MouseAxisCode::DeltaY:
                    return String(u8"Mouse dY");
                case input::MouseAxisCode::Wheel:
                    return String(u8"Mouse Wheel");
                }
                return String(u8"Mouse Axis");
            case input::BindingSource::MouseDelta:
                return String(u8"Mouse Delta (2D)");
            case input::BindingSource::GamepadButton:
                return String(PadButtonName(b.code));
            case input::BindingSource::GamepadAxis:
                switch (static_cast<foundation::shell::GamepadAxis>(b.code))
                {
                case foundation::shell::GamepadAxis::LeftX:
                    return String(u8"Pad Left X");
                case foundation::shell::GamepadAxis::LeftY:
                    return String(u8"Pad Left Y");
                case foundation::shell::GamepadAxis::RightX:
                    return String(u8"Pad Right X");
                case foundation::shell::GamepadAxis::RightY:
                    return String(u8"Pad Right Y");
                case foundation::shell::GamepadAxis::LeftTrigger:
                    return String(u8"Pad LT");
                case foundation::shell::GamepadAxis::RightTrigger:
                    return String(u8"Pad RT");
                default:
                    return String(u8"Pad Axis");
                }
            case input::BindingSource::GamepadStick:
                return String(static_cast<input::StickCode>(b.code) == input::StickCode::Left
                                  ? u8"Left Stick"
                                  : u8"Right Stick");
            case input::BindingSource::TouchButton:
                return String(u8"Touch Region");
            case input::BindingSource::TouchStick:
                return String(u8"Touch Stick");
            case input::BindingSource::Composite2D:
            {
                String s(u8"Keys ");
                s += KeyName(b.negX);
                s += u8"/";
                s += KeyName(b.posX);
                s += u8"/";
                s += KeyName(b.negY);
                s += u8"/";
                s += KeyName(b.posY);
                return s;
            }
            }
            return String(u8"?");
        }

        [[nodiscard]] inline StringView SourceName(input::BindingSource source)
        {
            switch (source)
            {
            case input::BindingSource::Key:
                return u8"Key";
            case input::BindingSource::MouseButton:
                return u8"MouseBtn";
            case input::BindingSource::MouseAxis:
                return u8"MouseAxis";
            case input::BindingSource::MouseDelta:
                return u8"MouseDelta";
            case input::BindingSource::GamepadButton:
                return u8"PadBtn";
            case input::BindingSource::GamepadAxis:
                return u8"PadAxis";
            case input::BindingSource::GamepadStick:
                return u8"PadStick";
            case input::BindingSource::Composite2D:
                return u8"Keys4";
            case input::BindingSource::TouchButton:
                return u8"TouchBtn";
            case input::BindingSource::TouchStick:
                return u8"TouchStick";
            }
            return u8"?";
        }

        // The sources an action of `kind` accepts, in cycle order (mirrors ValidateInputMap).
        inline usize ValidSources(input::ActionKind kind, input::BindingSource out[8])
        {
            usize n = 0;
            switch (kind)
            {
            case input::ActionKind::Button:
                out[n++] = input::BindingSource::Key;
                out[n++] = input::BindingSource::MouseButton;
                out[n++] = input::BindingSource::GamepadButton;
                out[n++] = input::BindingSource::TouchButton;
                break;
            case input::ActionKind::Axis1D:
                out[n++] = input::BindingSource::Key;
                out[n++] = input::BindingSource::MouseButton;
                out[n++] = input::BindingSource::GamepadButton;
                out[n++] = input::BindingSource::MouseAxis;
                out[n++] = input::BindingSource::GamepadAxis;
                break;
            case input::ActionKind::Axis2D:
                out[n++] = input::BindingSource::GamepadStick;
                out[n++] = input::BindingSource::Composite2D;
                out[n++] = input::BindingSource::MouseDelta;
                out[n++] = input::BindingSource::TouchStick;
                break;
            }
            return n;
        }

        [[nodiscard]] inline StringView KindName(input::ActionKind kind)
        {
            switch (kind)
            {
            case input::ActionKind::Button:
                return u8"Button";
            case input::ActionKind::Axis1D:
                return u8"Axis1D";
            case input::ActionKind::Axis2D:
                return u8"Axis2D";
            }
            return u8"?";
        }

        [[nodiscard]] inline StringView InteractionName(input::InteractionKind kind)
        {
            switch (kind)
            {
            case input::InteractionKind::None:
                return u8"On Press";
            case input::InteractionKind::Hold:
                return u8"Hold";
            case input::InteractionKind::Tap:
                return u8"Tap";
            case input::InteractionKind::DoubleTap:
                return u8"Double Tap";
            }
            return u8"?";
        }
    }

    class InputMapEditorPage final : public app::UIEditorPage
    {
    public:
        InputMapEditorPage(EditorContext& context, runtime::IApplicationHost&,
                           foundation::content::Instance& instance)
            : m_context(&context), m_title(instance.Name())
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            if (auto* asset = Cast<pipeline::InputMapAsset>(object.Get()))
            {
                m_map = asset->Map();
            }

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Padding = ui::Thickness{8, 6};

            m_status = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(20));
                column->AddView(m_status.Get(), lp);
            }

            m_scroll = MakeRef<ui::ScrollView>(DefaultAllocator());
            m_rows = MakeRef<ui::FlexLayout>(DefaultAllocator());
            m_rows->Direction = ui::Orientation::Vertical;
            m_rows->Spacing = 2.0f;
            m_scroll->AddView(m_rows.Get());
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Grow = 1.0f;
                column->AddView(m_scroll.Get(), lp);
            }
            m_content = column;
            Rebuild();
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] ui::View* ContentView() override { return m_content.Get(); }

        [[nodiscard]] Status Save() override;

        // Per-frame: rebind capture while listening (Esc cancels).
        void OnUpdate(runtime::IApplicationHost& host, f32) override;

    private:
        // Every mutation = one undoable command over whole-map snapshots (small data).
        class MapEditCommand final : public IEditorCommand
        {
        public:
            MapEditCommand(InputMapEditorPage& page, input::InputMap before, input::InputMap after)
                : m_page(&page), m_before(static_cast<input::InputMap&&>(before)),
                  m_after(static_cast<input::InputMap&&>(after))
            {
            }
            [[nodiscard]] bool Execute() override
            {
                m_page->m_map = m_after;
                m_page->RequestRebuild();
                return true;
            }
            void Undo() override
            {
                m_page->m_map = m_before;
                m_page->RequestRebuild();
            }
            [[nodiscard]] StringView TypeId() const override { return u8"input-map-edit"; }

        private:
            InputMapEditorPage* m_page;
            input::InputMap m_before;
            input::InputMap m_after;
        };

        template <typename Fn>
        void Mutate(Fn&& fn)
        {
            input::InputMap before = m_map;
            input::InputMap after = m_map;
            fn(after);
            (void)Commands().Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<MapEditCommand>(*this,
                                                       static_cast<input::InputMap&&>(before),
                                                       static_cast<input::InputMap&&>(after)),
                DefaultAllocator()));
        }

        ui::Button* MakeButton(ui::FlexLayout& row, StringView label, f32 width,
                               Function<void()> onClick);

        [[nodiscard]] RefPtr<ui::FlexLayout> MakeRow(f32 indent, f32 height = 24.0f);

        void AddLabel(ui::FlexLayout& row, StringView text, f32 grow = 0.0f, f32 width = 0.0f);

        // A small labeled numeric field: click-to-edit value, committed via parse. The
        // mutation receives the parsed float.
        void AddFloatField(ui::FlexLayout& row, StringView label, f32 value,
                           Function<void(f32)> commit, f32 width = 46.0f);

        void AddToggle(ui::FlexLayout& row, StringView label, bool value,
                       Function<void(bool)> commit);

        void AddNameEditor(ui::FlexLayout& row, StringView name, Function<void(StringView)> commit);

        /// Defer a Rebuild through the UI MutationQueue - call this instead of Rebuild() from anything
        /// that runs DURING UI event dispatch (a row/button click, an undo/redo, a rebind). Rebuilding
        /// the rows in-line frees the clicked button, then FireClick + DispatchMouseUp dereference the
        /// freed view -> crash; the queue runs the rebuild AFTER dispatch drains. Rebuild() itself stays
        /// for the initial (setup-time) build.
        void RequestRebuild();

        void Rebuild();

        void BeginListen(usize set, usize action, usize binding, i32 compositeDirection = -1);

        // Per-field mutation shorthand.
        template <typename Apply>
        void MutateAction(usize s, usize a, Apply&& apply)
        {
            Mutate(
                [s, a, apply](input::InputMap& m)
                {
                    if (s >= m.sets.Size() || a >= m.sets[s].actions.Size())
                    {
                        return;
                    }
                    apply(m.sets[s].actions[a]);
                });
        }

        template <typename Apply>
        void MutateBinding(usize s, usize a, usize b, Apply&& apply)
        {
            Mutate(
                [s, a, b, apply](input::InputMap& m)
                {
                    if (s >= m.sets.Size() || a >= m.sets[s].actions.Size())
                    {
                        return;
                    }
                    auto& bindings = m.sets[s].actions[a].bindings;
                    if (b < bindings.Size())
                    {
                        apply(bindings[b]);
                    }
                });
        }

        // The source-specific scalar fields, on their own indented line. Only fields the
        // source actually reads appear.
        void BuildBindingDetail(usize s, usize a, usize b, const input::Binding& binding);

        void RefreshStatus();

        EditorContext* m_context = nullptr;
        String m_title;
        input::InputMap m_map;

        RefPtr<ui::View> m_content;
        RefPtr<ui::ScrollView> m_scroll;
        RefPtr<ui::FlexLayout> m_rows;
        RefPtr<ui::Label> m_status;

        bool m_listening = false;
        usize m_listenSet = 0;
        usize m_listenAction = 0;
        usize m_listenBinding = 0;
        i32 m_listenDirection = -1; // >= 0: capturing one Composite2D direction key
        input::CaptureFilter m_listenFilter;
    };

    class InputMapPageFactory final : public IEditorPageFactory
    {
    public:
        explicit InputMapPageFactory(runtime::IApplicationHost& host) : m_host(&host) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override;
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override;

    private:
        runtime::IApplicationHost* m_host;
    };

    /// The editor executable's entry point for the input plugin.
    inline void RegisterInputEditor(EditorContext& context, runtime::IApplicationHost& host)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<InputMapPageFactory>(host), DefaultAllocator()));
    }

}
