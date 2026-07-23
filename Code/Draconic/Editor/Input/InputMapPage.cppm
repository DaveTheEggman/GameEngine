// Draconic::EditorInput - the `draconic.editor.input` module.
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

export module draconic.editor.input;

import draconic.core;
import draconic.content;
import draconic.shell;
import draconic.runtime;
import draconic.runtime.client;
import draconic.input;
import draconic.input.editor;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.ui.runtime;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace runtime = draconic::runtime;
    namespace ui = draconic::ui;
    namespace input = draconic::input;

    namespace detail
    {
        [[nodiscard]] inline String KeyName(u32 code)
        {
            namespace shell = draconic::shell;
            const shell::KeyCode key = static_cast<shell::KeyCode>(code);
            const u32 a = static_cast<u32>(shell::KeyCode::A);
            const u32 z = static_cast<u32>(shell::KeyCode::Z);
            const u32 n0 = static_cast<u32>(shell::KeyCode::Num0);
            const u32 n9 = static_cast<u32>(shell::KeyCode::Num9);
            const u32 f1 = static_cast<u32>(shell::KeyCode::F1);
            const u32 f24 = static_cast<u32>(shell::KeyCode::F24);
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
            case shell::KeyCode::Return:
                return String(u8"Return");
            case shell::KeyCode::Escape:
                return String(u8"Escape");
            case shell::KeyCode::Backspace:
                return String(u8"Backspace");
            case shell::KeyCode::Tab:
                return String(u8"Tab");
            case shell::KeyCode::Space:
                return String(u8"Space");
            case shell::KeyCode::Left:
                return String(u8"Left");
            case shell::KeyCode::Right:
                return String(u8"Right");
            case shell::KeyCode::Up:
                return String(u8"Up");
            case shell::KeyCode::Down:
                return String(u8"Down");
            case shell::KeyCode::LeftShift:
                return String(u8"LShift");
            case shell::KeyCode::RightShift:
                return String(u8"RShift");
            case shell::KeyCode::LeftCtrl:
                return String(u8"LCtrl");
            case shell::KeyCode::RightCtrl:
                return String(u8"RCtrl");
            case shell::KeyCode::LeftAlt:
                return String(u8"LAlt");
            case shell::KeyCode::RightAlt:
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
            switch (static_cast<shell::GamepadButton>(code))
            {
            case shell::GamepadButton::South:
                return u8"Pad South";
            case shell::GamepadButton::East:
                return u8"Pad East";
            case shell::GamepadButton::West:
                return u8"Pad West";
            case shell::GamepadButton::North:
                return u8"Pad North";
            case shell::GamepadButton::LeftShoulder:
                return u8"Pad LB";
            case shell::GamepadButton::RightShoulder:
                return u8"Pad RB";
            case shell::GamepadButton::DPadUp:
                return u8"DPad Up";
            case shell::GamepadButton::DPadDown:
                return u8"DPad Down";
            case shell::GamepadButton::DPadLeft:
                return u8"DPad Left";
            case shell::GamepadButton::DPadRight:
                return u8"DPad Right";
            case shell::GamepadButton::Start:
                return u8"Pad Start";
            case shell::GamepadButton::Back:
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
                switch (static_cast<shell::MouseButton>(b.code))
                {
                case shell::MouseButton::Left:
                    return String(u8"Mouse Left");
                case shell::MouseButton::Right:
                    return String(u8"Mouse Right");
                case shell::MouseButton::Middle:
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
                switch (static_cast<shell::GamepadAxis>(b.code))
                {
                case shell::GamepadAxis::LeftX:
                    return String(u8"Pad Left X");
                case shell::GamepadAxis::LeftY:
                    return String(u8"Pad Left Y");
                case shell::GamepadAxis::RightX:
                    return String(u8"Pad Right X");
                case shell::GamepadAxis::RightY:
                    return String(u8"Pad Right Y");
                case shell::GamepadAxis::LeftTrigger:
                    return String(u8"Pad LT");
                case shell::GamepadAxis::RightTrigger:
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
                           draconic::content::Instance& instance)
            : m_context(&context), m_title(instance.Name())
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            if (auto* asset = Cast<input::InputMapAsset>(object.Get()))
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

        [[nodiscard]] Status Save() override
        {
            String error;
            if (!input::ValidateInputMap(m_map, &error))
            {
                String message(u8"Input map invalid: ");
                message += error;
                m_context->Notify(NoticeKind::Error, message.AsView());
                return Status{ErrorCode::InvalidArgument};
            }
            draconic::content::Instance* instance =
                (m_context->Project() != nullptr)
                    ? m_context->Project()->SourceDb().GetInstance(InstanceId())
                    : nullptr;
            if (instance == nullptr)
            {
                return Status{ErrorCode::NotFound};
            }
            input::InputMapAsset asset;
            asset.Map() = m_map;
            const Status written = instance->WriteObject(asset);
            if (written.IsOk())
            {
                ClearDirty();
            }
            return written;
        }

        // Per-frame: rebind capture while listening (Esc cancels).
        void OnUpdate(runtime::IApplicationHost& host, f32) override
        {
            if (!m_listening)
            {
                return;
            }
            auto* shellInput = host.Shell() != nullptr ? host.Shell()->Input() : nullptr;
            if (shellInput == nullptr)
            {
                return;
            }
            if (shellInput->Keyboard() != nullptr &&
                shellInput->Keyboard()->IsKeyPressed(draconic::shell::KeyCode::Escape))
            {
                m_listening = false;
                RefreshStatus();
                return;
            }
            input::ShellInputSource devices(shellInput);
            input::Binding captured;
            if (input::CaptureBinding(devices, m_listenFilter, captured))
            {
                const usize set = m_listenSet;
                const usize action = m_listenAction;
                const usize binding = m_listenBinding;
                const i32 direction = m_listenDirection;
                m_listening = false;
                m_listenDirection = -1;
                Mutate(
                    [set, action, binding, direction, captured](input::InputMap& map)
                    {
                        if (set >= map.sets.Size())
                        {
                            return;
                        }
                        if (action >= map.sets[set].actions.Size())
                        {
                            return;
                        }
                        auto& bindings = map.sets[set].actions[action].bindings;
                        if (binding >= bindings.Size())
                        {
                            return;
                        }
                        if (direction < 0)
                        {
                            bindings[binding] = captured;
                            return;
                        }
                        // Composite direction: swap in just the captured KEY code.
                        u32* slot = direction == 0   ? &bindings[binding].negX
                                    : direction == 1 ? &bindings[binding].posX
                                    : direction == 2 ? &bindings[binding].negY
                                                     : &bindings[binding].posY;
                        *slot = captured.code;
                    });
            }
        }

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
                               Function<void()> onClick)
        {
            auto button = MakeRef<ui::Button>(DefaultAllocator(), label);
            button->FontSize.SetValue(Optional<f32>{11.0f});
            button->OnClick.Add(
                [fn = Move(onClick)](ui::ButtonBase*)
                {
                    if (fn)
                    {
                        fn();
                    }
                });
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
            lp->Height = ui::SizeSpec::Match();
            row.AddView(button.Get(), lp);
            return button.Get();
        }

        [[nodiscard]] RefPtr<ui::FlexLayout> MakeRow(f32 indent, f32 height = 24.0f)
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 4.0f;
            row->Padding = ui::Thickness{indent, 0};
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(height));
            m_rows->AddView(row.Get(), lp);
            return row;
        }

        void AddLabel(ui::FlexLayout& row, StringView text, f32 grow = 0.0f, f32 width = 0.0f)
        {
            auto label = MakeRef<ui::Label>(DefaultAllocator(), text);
            label->FontSize.SetValue(12.0f);
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            if (grow > 0.0f)
            {
                lp->Grow = grow;
            }
            else if (width > 0.0f)
            {
                lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
            }
            lp->Height = ui::SizeSpec::Match();
            row.AddView(label.Get(), lp);
        }

        // A small labeled numeric field: click-to-edit value, committed via parse. The
        // mutation receives the parsed float.
        void AddFloatField(ui::FlexLayout& row, StringView label, f32 value,
                           Function<void(f32)> commit, f32 width = 46.0f)
        {
            AddLabel(row, label, 0.0f, static_cast<f32>(label.Size()) * 7.0f + 6.0f);
            auto field = MakeRef<ui::EditableLabel>(DefaultAllocator());
            String text;
            AppendValue(text, value);
            field->SetText(text.AsView());
            field->FontSize.SetValue(12.0f);
            field->OnRenameCommitted.Add(
                [fn = Move(commit)](ui::EditableLabel*, StringView committed)
                {
                    if (!fn || committed.IsEmpty())
                    {
                        return;
                    }
                    String buffer(committed);
                    char* end = nullptr;
                    const f32 parsed =
                        std::strtof(reinterpret_cast<const char*>(buffer.CStr()), &end);
                    if (end != reinterpret_cast<const char*>(buffer.CStr()))
                    {
                        fn(parsed);
                    }
                });
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(width));
            lp->Height = ui::SizeSpec::Match();
            row.AddView(field.Get(), lp);
        }

        void AddToggle(ui::FlexLayout& row, StringView label, bool value,
                       Function<void(bool)> commit)
        {
            String text(label);
            text += value ? StringView(u8":on") : StringView(u8":off");
            MakeButton(row, text.AsView(), static_cast<f32>(text.Size()) * 7.0f + 14.0f,
                       [fn = Move(commit), value]()
                       {
                           if (fn)
                           {
                               fn(!value);
                           }
                       });
        }

        void AddNameEditor(ui::FlexLayout& row, StringView name, Function<void(StringView)> commit)
        {
            auto label = MakeRef<ui::EditableLabel>(DefaultAllocator());
            label->SetText(name);
            label->FontSize.SetValue(12.0f);
            label->OnRenameCommitted.Add(
                [fn = Move(commit)](ui::EditableLabel*, StringView value)
                {
                    if (fn && !value.IsEmpty())
                    {
                        fn(value);
                    }
                });
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            lp->Height = ui::SizeSpec::Match();
            row.AddView(label.Get(), lp);
        }

        /// Defer a Rebuild through the UI MutationQueue - call this instead of Rebuild() from anything
        /// that runs DURING UI event dispatch (a row/button click, an undo/redo, a rebind). Rebuilding
        /// the rows in-line frees the clicked button, then FireClick + DispatchMouseUp dereference the
        /// freed view -> crash; the queue runs the rebuild AFTER dispatch drains. Rebuild() itself stays
        /// for the initial (setup-time) build.
        void RequestRebuild()
        {
            ui::UIContext* ctx = (m_rows.Get() != nullptr) ? m_rows->Context : nullptr;
            if (ctx == nullptr)
            {
                Rebuild();
                return;
            } // not attached yet (setup) - safe to do now
            InputMapEditorPage* self = this;
            ctx->MutationQueueRef().QueueAction(Function<void()>{[self]() { self->Rebuild(); }});
        }

        void Rebuild()
        {
            m_rows->RemoveAllViews();
            InputMapEditorPage* self = this;

            for (usize s = 0; s < m_map.sets.Size(); ++s)
            {
                const input::ActionSet& set = m_map.sets[s];
                auto header = MakeRow(0.0f, 26.0f);
                AddNameEditor(*header, set.name.AsView(),
                              [self, s](StringView value)
                              {
                                  String name(value);
                                  self->Mutate(
                                      [s, name](input::InputMap& m)
                                      {
                                          if (s < m.sets.Size())
                                          {
                                              m.sets[s].name = name;
                                          }
                                      });
                              });
                String priority(u8"prio ");
                AppendValue(priority, static_cast<i64>(set.priority));
                AddLabel(*header, priority.AsView(), 0.0f, 52.0f);
                MakeButton(*header, u8"+", 22.0f,
                           [self, s]()
                           {
                               self->Mutate(
                                   [s](input::InputMap& m)
                                   {
                                       if (s < m.sets.Size())
                                       {
                                           m.sets[s].priority += 1;
                                       }
                                   });
                           });
                MakeButton(*header, u8"-", 22.0f,
                           [self, s]()
                           {
                               self->Mutate(
                                   [s](input::InputMap& m)
                                   {
                                       if (s < m.sets.Size())
                                       {
                                           m.sets[s].priority -= 1;
                                       }
                                   });
                           });
                MakeButton(*header, u8"+ Action", 70.0f,
                           [self, s]()
                           {
                               self->Mutate(
                                   [s](input::InputMap& m)
                                   {
                                       if (s >= m.sets.Size())
                                       {
                                           return;
                                       }
                                       input::Action action;
                                       action.name = String(u8"NewAction");
                                       m.sets[s].actions.PushBack(
                                           static_cast<input::Action&&>(action));
                                   });
                           });
                MakeButton(*header, u8"x", 22.0f,
                           [self, s]()
                           {
                               self->Mutate(
                                   [s](input::InputMap& m)
                                   {
                                       if (s < m.sets.Size())
                                       {
                                           m.sets.RemoveAt(s);
                                       }
                                   });
                           });

                for (usize a = 0; a < set.actions.Size(); ++a)
                {
                    const input::Action& action = set.actions[a];
                    auto row = MakeRow(18.0f);
                    AddNameEditor(*row, action.name.AsView(),
                                  [self, s, a](StringView value)
                                  {
                                      String name(value);
                                      self->Mutate(
                                          [s, a, name](input::InputMap& m)
                                          {
                                              if (s < m.sets.Size() && a < m.sets[s].actions.Size())
                                              {
                                                  m.sets[s].actions[a].name = name;
                                              }
                                          });
                                  });
                    MakeButton(*row, detail::KindName(action.kind), 60.0f,
                               [self, s, a]()
                               {
                                   self->Mutate(
                                       [s, a](input::InputMap& m)
                                       {
                                           if (s >= m.sets.Size() || a >= m.sets[s].actions.Size())
                                           {
                                               return;
                                           }
                                           input::Action& act = m.sets[s].actions[a];
                                           act.kind = static_cast<input::ActionKind>(
                                               (static_cast<u8>(act.kind) + 1u) % 3u);
                                       });
                               });
                    MakeButton(
                        *row, detail::InteractionName(action.interaction.kind), 80.0f,
                        [self, s, a]()
                        {
                            self->Mutate(
                                [s, a](input::InputMap& m)
                                {
                                    if (s >= m.sets.Size() || a >= m.sets[s].actions.Size())
                                    {
                                        return;
                                    }
                                    input::Action& act = m.sets[s].actions[a];
                                    act.interaction.kind = static_cast<input::InteractionKind>(
                                        (static_cast<u8>(act.interaction.kind) + 1u) % 4u);
                                });
                        });
                    MakeButton(*row, u8"+ Binding", 74.0f,
                               [self, s, a]()
                               {
                                   self->Mutate(
                                       [s, a](input::InputMap& m)
                                       {
                                           if (s >= m.sets.Size() || a >= m.sets[s].actions.Size())
                                           {
                                               return;
                                           }
                                           input::Action& act = m.sets[s].actions[a];
                                           input::Binding fresh;
                                           if (act.kind == input::ActionKind::Axis2D)
                                           {
                                               fresh.source = input::BindingSource::GamepadStick;
                                           }
                                           act.bindings.PushBack(fresh);
                                       });
                               });
                    MakeButton(*row, u8"x", 22.0f,
                               [self, s, a]()
                               {
                                   self->Mutate(
                                       [s, a](input::InputMap& m)
                                       {
                                           if (s < m.sets.Size() && a < m.sets[s].actions.Size())
                                           {
                                               m.sets[s].actions.RemoveAt(a);
                                           }
                                       });
                               });

                    for (usize b = 0; b < action.bindings.Size(); ++b)
                    {
                        const input::Binding& binding = action.bindings[b];
                        auto bindingRow = MakeRow(40.0f, 22.0f);
                        const bool isListening = m_listening && m_listenSet == s &&
                                                 m_listenAction == a && m_listenBinding == b;
                        // Source cycles through the KIND's valid sources.
                        MakeButton(
                            *bindingRow, detail::SourceName(binding.source), 76.0f,
                            [self, s, a, b]()
                            {
                                self->Mutate(
                                    [s, a, b](input::InputMap& m)
                                    {
                                        if (s >= m.sets.Size() || a >= m.sets[s].actions.Size())
                                        {
                                            return;
                                        }
                                        auto& act = m.sets[s].actions[a];
                                        if (b >= act.bindings.Size())
                                        {
                                            return;
                                        }
                                        input::BindingSource valid[8];
                                        const usize n = detail::ValidSources(act.kind, valid);
                                        usize current = 0;
                                        for (usize i = 0; i < n; ++i)
                                        {
                                            if (valid[i] == act.bindings[b].source)
                                            {
                                                current = i;
                                                break;
                                            }
                                        }
                                        input::Binding
                                            fresh; // source change resets source-specifics
                                        fresh.source = valid[(current + 1) % n];
                                        act.bindings[b] = fresh;
                                    });
                            });
                        AddLabel(*bindingRow,
                                 isListening ? StringView(u8"<press an input...>")
                                             : detail::DescribeBinding(binding).AsView(),
                                 1.0f);
                        MakeButton(*bindingRow, u8"Listen", 54.0f,
                                   [self, s, a, b]() { self->BeginListen(s, a, b); });
                        MakeButton(*bindingRow, u8"x", 22.0f,
                                   [self, s, a, b]()
                                   {
                                       self->Mutate(
                                           [s, a, b](input::InputMap& m)
                                           {
                                               if (s >= m.sets.Size() ||
                                                   a >= m.sets[s].actions.Size())
                                               {
                                                   return;
                                               }
                                               auto& bindings = m.sets[s].actions[a].bindings;
                                               if (b < bindings.Size())
                                               {
                                                   bindings.RemoveAt(b);
                                               }
                                           });
                                   });
                        BuildBindingDetail(s, a, b, binding);
                    }

                    // Processors (+ interaction window) on their own line.
                    {
                        auto proc = MakeRow(40.0f, 20.0f);
                        if (action.interaction.kind != input::InteractionKind::None)
                        {
                            AddFloatField(*proc, u8"sec", action.interaction.seconds,
                                          [self, s, a](f32 v)
                                          {
                                              self->MutateAction(s, a, [v](input::Action& x)
                                                                 { x.interaction.seconds = v; });
                                          });
                        }
                        if (action.kind != input::ActionKind::Button)
                        {
                            AddFloatField(*proc, u8"sens", action.processors.sensitivity,
                                          [self, s, a](f32 v)
                                          {
                                              self->MutateAction(s, a, [v](input::Action& x)
                                                                 { x.processors.sensitivity = v; });
                                          });
                            AddFloatField(*proc, u8"grav", action.processors.gravity,
                                          [self, s, a](f32 v)
                                          {
                                              self->MutateAction(s, a, [v](input::Action& x)
                                                                 { x.processors.gravity = v; });
                                          });
                            AddToggle(*proc, u8"snap", action.processors.snap,
                                      [self, s, a](bool v)
                                      {
                                          self->MutateAction(s, a, [v](input::Action& x)
                                                             { x.processors.snap = v; });
                                      });
                            AddFloatField(*proc, u8"curve", action.processors.responseExponent,
                                          [self, s, a](f32 v)
                                          {
                                              self->MutateAction(
                                                  s, a, [v](input::Action& x)
                                                  { x.processors.responseExponent = v; });
                                          });
                            AddToggle(*proc, u8"tScale", action.processors.timeScale,
                                      [self, s, a](bool v)
                                      {
                                          self->MutateAction(s, a, [v](input::Action& x)
                                                             { x.processors.timeScale = v; });
                                      });
                        }
                    }
                }
            }

            auto footer = MakeRow(0.0f, 26.0f);
            MakeButton(*footer, u8"+ Add Set", 90.0f,
                       [self]()
                       {
                           self->Mutate(
                               [](input::InputMap& m)
                               {
                                   input::ActionSet set;
                                   set.name = String(u8"NewSet");
                                   m.sets.PushBack(static_cast<input::ActionSet&&>(set));
                               });
                       });

            RefreshStatus();
            m_content->Invalidate();
        }

        void BeginListen(usize set, usize action, usize binding, i32 compositeDirection = -1)
        {
            m_listening = true;
            m_listenSet = set;
            m_listenAction = action;
            m_listenBinding = binding;
            m_listenDirection = compositeDirection;
            if (compositeDirection >= 0)
            {
                // A composite direction rebind is always a single KEY.
                input::CaptureFilter keysOnly;
                keysOnly.mouseButtons = false;
                keysOnly.gamepadButtons = false;
                m_listenFilter = keysOnly;
                RequestRebuild();
                return;
            }
            // Filter by the action's declared kind: a Button rebind ignores stick noise,
            // an Axis2D rebind captures sticks only.
            input::CaptureFilter filter;
            if (set < m_map.sets.Size() && action < m_map.sets[set].actions.Size())
            {
                switch (m_map.sets[set].actions[action].kind)
                {
                case input::ActionKind::Button:
                    break; // keys + mouse + pad buttons
                case input::ActionKind::Axis1D:
                    filter.gamepadAxes = true;
                    break;
                case input::ActionKind::Axis2D:
                    filter.keys = false;
                    filter.mouseButtons = false;
                    filter.gamepadButtons = false;
                    filter.gamepadSticks = true;
                    break;
                }
            }
            m_listenFilter = filter;
            Rebuild();
        }

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
        void BuildBindingDetail(usize s, usize a, usize b, const input::Binding& binding)
        {
            using Source = input::BindingSource;
            const Source source = binding.source;
            const bool hasDeadZone = source == Source::MouseAxis || source == Source::GamepadAxis ||
                                     source == Source::GamepadStick || source == Source::TouchStick;
            const bool hasScale = source != Source::TouchButton;
            const bool hasInvert = source == Source::MouseAxis || source == Source::MouseDelta ||
                                   source == Source::GamepadAxis ||
                                   source == Source::GamepadStick || source == Source::TouchStick ||
                                   source == Source::Composite2D;
            const bool hasDevice = source == Source::GamepadButton ||
                                   source == Source::GamepadAxis || source == Source::GamepadStick;
            const bool hasRegion = source == Source::TouchButton || source == Source::TouchStick;
            InputMapEditorPage* self = this;

            auto detail = MakeRow(62.0f, 20.0f);
            if (hasDeadZone)
            {
                AddFloatField(
                    *detail, u8"dz", binding.deadZone, [self, s, a, b](f32 v)
                    { self->MutateBinding(s, a, b, [v](input::Binding& x) { x.deadZone = v; }); });
            }
            if (hasScale)
            {
                AddFloatField(
                    *detail, u8"scale", binding.scale, [self, s, a, b](f32 v)
                    { self->MutateBinding(s, a, b, [v](input::Binding& x) { x.scale = v; }); });
            }
            if (hasInvert)
            {
                AddToggle(
                    *detail, u8"inv", binding.invert, [self, s, a, b](bool v)
                    { self->MutateBinding(s, a, b, [v](input::Binding& x) { x.invert = v; }); });
            }
            if (hasDevice)
            {
                AddFloatField(*detail, u8"pad", static_cast<f32>(binding.device),
                              [self, s, a, b](f32 v)
                              {
                                  self->MutateBinding(s, a, b, [v](input::Binding& x)
                                                      { x.device = static_cast<i32>(v); });
                              });
            }
            if (source == Source::Composite2D)
            {
                AddToggle(
                    *detail, u8"norm", binding.normalize, [self, s, a, b](bool v)
                    { self->MutateBinding(s, a, b, [v](input::Binding& x) { x.normalize = v; }); });
                // Per-direction key capture: -X +X -Y +Y each Listen for one key.
                const StringView labels[] = {u8"-X", u8"+X", u8"-Y", u8"+Y"};
                for (u32 d = 0; d < 4; ++d)
                {
                    String text(labels[d]);
                    text += u8" ";
                    const u32 code = d == 0   ? binding.negX
                                     : d == 1 ? binding.posX
                                     : d == 2 ? binding.negY
                                              : binding.posY;
                    text += detail::KeyName(code);
                    MakeButton(*detail, text.AsView(), 74.0f, [self, s, a, b, d]()
                               { self->BeginListen(s, a, b, static_cast<i32>(d)); });
                }
            }
            if (hasRegion)
            {
                AddFloatField(
                    *detail, u8"rx", binding.regionX, [self, s, a, b](f32 v)
                    { self->MutateBinding(s, a, b, [v](input::Binding& x) { x.regionX = v; }); });
                AddFloatField(
                    *detail, u8"ry", binding.regionY, [self, s, a, b](f32 v)
                    { self->MutateBinding(s, a, b, [v](input::Binding& x) { x.regionY = v; }); });
                AddFloatField(
                    *detail, u8"rw", binding.regionW, [self, s, a, b](f32 v)
                    { self->MutateBinding(s, a, b, [v](input::Binding& x) { x.regionW = v; }); });
                AddFloatField(
                    *detail, u8"rh", binding.regionH, [self, s, a, b](f32 v)
                    { self->MutateBinding(s, a, b, [v](input::Binding& x) { x.regionH = v; }); });
            }
            if (source == Source::TouchStick)
            {
                AddFloatField(*detail, u8"radius", binding.stickRadius,
                              [self, s, a, b](f32 v)
                              {
                                  self->MutateBinding(s, a, b, [v](input::Binding& x)
                                                      { x.stickRadius = v; });
                              });
            }
        }

        void RefreshStatus()
        {
            if (m_listening)
            {
                m_status->SetText(u8"Listening... press the new input (Esc cancels)");
                return;
            }
            String error;
            if (!input::ValidateInputMap(m_map, &error))
            {
                String message(u8"Invalid: ");
                message += error;
                m_status->SetText(message.AsView());
            }
            else
            {
                m_status->SetText(
                    u8"Sets > actions > bindings. Click names to rename; Listen rebinds.");
            }
        }

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
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &input::InputMapAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, draconic::content::Instance& instance) override
        {
            auto* page = DefaultAllocator().New<InputMapEditorPage>(context, *m_host, instance);
            return UniquePtr<EditorPage>(page, DefaultAllocator());
        }

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
