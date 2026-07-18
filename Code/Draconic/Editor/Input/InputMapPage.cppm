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
    namespace irt = draconic::runtime;
    namespace iui = draconic::ui;
    namespace itk = draconic::ui::toolkit;
    namespace iuirt = draconic::ui::runtime;
    namespace din = draconic::input;

    namespace detail
    {
        [[nodiscard]] inline String KeyName(u32 code)
        {
            namespace sh = draconic::shell;
            const sh::KeyCode key = static_cast<sh::KeyCode>(code);
            const u32 a = static_cast<u32>(sh::KeyCode::A);
            const u32 z = static_cast<u32>(sh::KeyCode::Z);
            const u32 n0 = static_cast<u32>(sh::KeyCode::Num0);
            const u32 n9 = static_cast<u32>(sh::KeyCode::Num9);
            const u32 f1 = static_cast<u32>(sh::KeyCode::F1);
            const u32 f24 = static_cast<u32>(sh::KeyCode::F24);
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
                case sh::KeyCode::Return: return String(u8"Return");
                case sh::KeyCode::Escape: return String(u8"Escape");
                case sh::KeyCode::Backspace: return String(u8"Backspace");
                case sh::KeyCode::Tab: return String(u8"Tab");
                case sh::KeyCode::Space: return String(u8"Space");
                case sh::KeyCode::Left: return String(u8"Left");
                case sh::KeyCode::Right: return String(u8"Right");
                case sh::KeyCode::Up: return String(u8"Up");
                case sh::KeyCode::Down: return String(u8"Down");
                case sh::KeyCode::LeftShift: return String(u8"LShift");
                case sh::KeyCode::RightShift: return String(u8"RShift");
                case sh::KeyCode::LeftCtrl: return String(u8"LCtrl");
                case sh::KeyCode::RightCtrl: return String(u8"RCtrl");
                case sh::KeyCode::LeftAlt: return String(u8"LAlt");
                case sh::KeyCode::RightAlt: return String(u8"RAlt");
                default: break;
            }
            out.Append(u8"Key#");
            AppendValue(out, static_cast<u64>(code));
            return out;
        }

        [[nodiscard]] inline StringView PadButtonName(u32 code)
        {
            namespace sh = draconic::shell;
            switch (static_cast<sh::GamepadButton>(code))
            {
                case sh::GamepadButton::South: return u8"Pad South";
                case sh::GamepadButton::East: return u8"Pad East";
                case sh::GamepadButton::West: return u8"Pad West";
                case sh::GamepadButton::North: return u8"Pad North";
                case sh::GamepadButton::LeftShoulder: return u8"Pad LB";
                case sh::GamepadButton::RightShoulder: return u8"Pad RB";
                case sh::GamepadButton::DPadUp: return u8"DPad Up";
                case sh::GamepadButton::DPadDown: return u8"DPad Down";
                case sh::GamepadButton::DPadLeft: return u8"DPad Left";
                case sh::GamepadButton::DPadRight: return u8"DPad Right";
                case sh::GamepadButton::Start: return u8"Pad Start";
                case sh::GamepadButton::Back: return u8"Pad Back";
                default: return u8"Pad Button";
            }
        }

        [[nodiscard]] inline String DescribeBinding(const din::Binding& b)
        {
            namespace sh = draconic::shell;
            switch (b.source)
            {
                case din::BindingSource::Key: return KeyName(b.code);
                case din::BindingSource::MouseButton:
                    switch (static_cast<sh::MouseButton>(b.code))
                    {
                        case sh::MouseButton::Left: return String(u8"Mouse Left");
                        case sh::MouseButton::Right: return String(u8"Mouse Right");
                        case sh::MouseButton::Middle: return String(u8"Mouse Middle");
                        default: return String(u8"Mouse Button");
                    }
                case din::BindingSource::MouseAxis:
                    switch (static_cast<din::MouseAxisCode>(b.code))
                    {
                        case din::MouseAxisCode::DeltaX: return String(u8"Mouse dX");
                        case din::MouseAxisCode::DeltaY: return String(u8"Mouse dY");
                        case din::MouseAxisCode::Wheel: return String(u8"Mouse Wheel");
                    }
                    return String(u8"Mouse Axis");
                case din::BindingSource::MouseDelta: return String(u8"Mouse Delta (2D)");
                case din::BindingSource::GamepadButton: return String(PadButtonName(b.code));
                case din::BindingSource::GamepadAxis:
                    switch (static_cast<sh::GamepadAxis>(b.code))
                    {
                        case sh::GamepadAxis::LeftX: return String(u8"Pad Left X");
                        case sh::GamepadAxis::LeftY: return String(u8"Pad Left Y");
                        case sh::GamepadAxis::RightX: return String(u8"Pad Right X");
                        case sh::GamepadAxis::RightY: return String(u8"Pad Right Y");
                        case sh::GamepadAxis::LeftTrigger: return String(u8"Pad LT");
                        case sh::GamepadAxis::RightTrigger: return String(u8"Pad RT");
                        default: return String(u8"Pad Axis");
                    }
                case din::BindingSource::GamepadStick:
                    return String(static_cast<din::StickCode>(b.code) == din::StickCode::Left
                                  ? u8"Left Stick" : u8"Right Stick");
                case din::BindingSource::Composite2D:
                {
                    String s(u8"Keys ");
                    s += KeyName(b.negX); s += u8"/";
                    s += KeyName(b.posX); s += u8"/";
                    s += KeyName(b.negY); s += u8"/";
                    s += KeyName(b.posY);
                    return s;
                }
            }
            return String(u8"?");
        }

        [[nodiscard]] inline StringView KindName(din::ActionKind kind)
        {
            switch (kind)
            {
                case din::ActionKind::Button: return u8"Button";
                case din::ActionKind::Axis1D: return u8"Axis1D";
                case din::ActionKind::Axis2D: return u8"Axis2D";
            }
            return u8"?";
        }

        [[nodiscard]] inline StringView InteractionName(din::InteractionKind kind)
        {
            switch (kind)
            {
                case din::InteractionKind::None: return u8"On Press";
                case din::InteractionKind::Hold: return u8"Hold";
                case din::InteractionKind::Tap: return u8"Tap";
                case din::InteractionKind::DoubleTap: return u8"Double Tap";
            }
            return u8"?";
        }
    }

    class InputMapEditorPage final : public app::UIEditorPage
    {
    public:
        InputMapEditorPage(EditorContext& context, irt::IApplicationHost&,
                           draconic::content::Instance& instance)
            : m_context(&context), m_title(instance.Name())
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            if (auto* asset = Cast<din::InputMapAsset>(object.Get()))
            {
                m_map = asset->Map();
            }

            auto column = MakeRef<iui::FlexLayout>(DefaultAllocator());
            column->Direction = iui::Orientation::Vertical;
            column->Padding = iui::Thickness{ 8, 6 };

            m_status = MakeRef<iui::Label>(DefaultAllocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            {
                auto lp = MakeRef<iui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = iui::SizeSpec::Match();
                lp->Height = iui::SizeSpec::Fixed(iui::Unit::Px(20));
                column->AddView(m_status.Get(), lp);
            }

            m_scroll = MakeRef<iui::ScrollView>(DefaultAllocator());
            m_rows = MakeRef<iui::FlexLayout>(DefaultAllocator());
            m_rows->Direction = iui::Orientation::Vertical;
            m_rows->Spacing = 2.0f;
            m_scroll->AddView(m_rows.Get());
            {
                auto lp = MakeRef<iui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = iui::SizeSpec::Match();
                lp->Grow = 1.0f;
                column->AddView(m_scroll.Get(), lp);
            }
            m_content = column;
            Rebuild();
        }

        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] iui::View* ContentView() override { return m_content.Get(); }

        [[nodiscard]] Status Save() override
        {
            String error;
            if (!din::ValidateInputMap(m_map, &error))
            {
                String message(u8"Input map invalid: ");
                message += error;
                m_context->Notify(NoticeKind::Error, message.AsView());
                return Status{ ErrorCode::InvalidArgument };
            }
            draconic::content::Instance* instance =
                (m_context->Project() != nullptr)
                    ? m_context->Project()->SourceDb().GetInstance(InstanceId()) : nullptr;
            if (instance == nullptr) { return Status{ ErrorCode::NotFound }; }
            din::InputMapAsset asset;
            asset.Map() = m_map;
            const Status written = instance->WriteObject(asset);
            if (written.IsOk()) { ClearDirty(); }
            return written;
        }

        // Per-frame: rebind capture while listening (Esc cancels).
        void OnUpdate(irt::IApplicationHost& host, f32) override
        {
            if (!m_listening) { return; }
            auto* shellInput = host.Shell() != nullptr ? host.Shell()->Input() : nullptr;
            if (shellInput == nullptr) { return; }
            if (shellInput->Keyboard() != nullptr
                && shellInput->Keyboard()->IsKeyPressed(draconic::shell::KeyCode::Escape))
            {
                m_listening = false;
                RefreshStatus();
                return;
            }
            din::ShellInputSource devices(shellInput);
            din::Binding captured;
            if (din::CaptureBinding(devices, m_listenFilter, captured))
            {
                const usize set = m_listenSet;
                const usize action = m_listenAction;
                const usize binding = m_listenBinding;
                m_listening = false;
                Mutate([set, action, binding, captured](din::InputMap& map) {
                    if (set >= map.sets.Size()) { return; }
                    if (action >= map.sets[set].actions.Size()) { return; }
                    auto& bindings = map.sets[set].actions[action].bindings;
                    if (binding < bindings.Size()) { bindings[binding] = captured; }
                });
            }
        }

    private:
        // Every mutation = one undoable command over whole-map snapshots (small data).
        class MapEditCommand final : public IEditorCommand
        {
        public:
            MapEditCommand(InputMapEditorPage& page, din::InputMap before, din::InputMap after)
                : m_page(&page), m_before(static_cast<din::InputMap&&>(before))
                , m_after(static_cast<din::InputMap&&>(after)) {}
            [[nodiscard]] bool Execute() override
            {
                m_page->m_map = m_after;
                m_page->Rebuild();
                return true;
            }
            void Undo() override
            {
                m_page->m_map = m_before;
                m_page->Rebuild();
            }
            [[nodiscard]] StringView TypeId() const override { return u8"input-map-edit"; }

        private:
            InputMapEditorPage* m_page;
            din::InputMap m_before;
            din::InputMap m_after;
        };

        template <typename Fn>
        void Mutate(Fn&& fn)
        {
            din::InputMap before = m_map;
            din::InputMap after = m_map;
            fn(after);
            (void)Commands().Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<MapEditCommand>(*this,
                    static_cast<din::InputMap&&>(before), static_cast<din::InputMap&&>(after)),
                DefaultAllocator()));
        }

        iui::Button* MakeButton(iui::FlexLayout& row, StringView label, f32 width,
                                Function<void()> onClick)
        {
            auto button = MakeRef<iui::Button>(DefaultAllocator(), label);
            button->FontSize.SetValue(Optional<f32>{ 11.0f });
            button->OnClick.Add([fn = Move(onClick)](iui::ButtonBase*) { if (fn) { fn(); } });
            auto lp = MakeRef<iui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = iui::SizeSpec::Fixed(iui::Unit::Px(width));
            lp->Height = iui::SizeSpec::Match();
            row.AddView(button.Get(), lp);
            return button.Get();
        }

        [[nodiscard]] RefPtr<iui::FlexLayout> MakeRow(f32 indent, f32 height = 24.0f)
        {
            auto row = MakeRef<iui::FlexLayout>(DefaultAllocator());
            row->Direction = iui::Orientation::Horizontal;
            row->Spacing = 4.0f;
            row->Padding = iui::Thickness{ indent, 0 };
            auto lp = MakeRef<iui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = iui::SizeSpec::Match();
            lp->Height = iui::SizeSpec::Fixed(iui::Unit::Px(height));
            m_rows->AddView(row.Get(), lp);
            return row;
        }

        void AddLabel(iui::FlexLayout& row, StringView text, f32 grow = 0.0f, f32 width = 0.0f)
        {
            auto label = MakeRef<iui::Label>(DefaultAllocator(), text);
            label->FontSize.SetValue(12.0f);
            auto lp = MakeRef<iui::FlexLayoutParams>(DefaultAllocator());
            if (grow > 0.0f) { lp->Grow = grow; }
            else if (width > 0.0f) { lp->Width = iui::SizeSpec::Fixed(iui::Unit::Px(width)); }
            lp->Height = iui::SizeSpec::Match();
            row.AddView(label.Get(), lp);
        }

        void AddNameEditor(iui::FlexLayout& row, StringView name, Function<void(StringView)> commit)
        {
            auto label = MakeRef<iui::EditableLabel>(DefaultAllocator());
            label->SetText(name);
            label->FontSize.SetValue(12.0f);
            label->OnRenameCommitted.Add(
                [fn = Move(commit)](iui::EditableLabel*, StringView value) {
                    if (fn && !value.IsEmpty()) { fn(value); }
                });
            auto lp = MakeRef<iui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            lp->Height = iui::SizeSpec::Match();
            row.AddView(label.Get(), lp);
        }

        void Rebuild()
        {
            m_rows->RemoveAllViews();
            InputMapEditorPage* self = this;

            for (usize s = 0; s < m_map.sets.Size(); ++s)
            {
                const din::ActionSet& set = m_map.sets[s];
                auto header = MakeRow(0.0f, 26.0f);
                AddNameEditor(*header, set.name.AsView(), [self, s](StringView value) {
                    String name(value);
                    self->Mutate([s, name](din::InputMap& m) {
                        if (s < m.sets.Size()) { m.sets[s].name = name; }
                    });
                });
                String priority(u8"prio ");
                AppendValue(priority, static_cast<i64>(set.priority));
                AddLabel(*header, priority.AsView(), 0.0f, 52.0f);
                MakeButton(*header, u8"+", 22.0f, [self, s]() {
                    self->Mutate([s](din::InputMap& m) {
                        if (s < m.sets.Size()) { m.sets[s].priority += 1; }
                    });
                });
                MakeButton(*header, u8"-", 22.0f, [self, s]() {
                    self->Mutate([s](din::InputMap& m) {
                        if (s < m.sets.Size()) { m.sets[s].priority -= 1; }
                    });
                });
                MakeButton(*header, u8"+ Action", 70.0f, [self, s]() {
                    self->Mutate([s](din::InputMap& m) {
                        if (s >= m.sets.Size()) { return; }
                        din::Action action;
                        action.name = String(u8"NewAction");
                        m.sets[s].actions.PushBack(static_cast<din::Action&&>(action));
                    });
                });
                MakeButton(*header, u8"x", 22.0f, [self, s]() {
                    self->Mutate([s](din::InputMap& m) {
                        if (s < m.sets.Size()) { m.sets.RemoveAt(s); }
                    });
                });

                for (usize a = 0; a < set.actions.Size(); ++a)
                {
                    const din::Action& action = set.actions[a];
                    auto row = MakeRow(18.0f);
                    AddNameEditor(*row, action.name.AsView(), [self, s, a](StringView value) {
                        String name(value);
                        self->Mutate([s, a, name](din::InputMap& m) {
                            if (s < m.sets.Size() && a < m.sets[s].actions.Size())
                            {
                                m.sets[s].actions[a].name = name;
                            }
                        });
                    });
                    MakeButton(*row, detail::KindName(action.kind), 60.0f, [self, s, a]() {
                        self->Mutate([s, a](din::InputMap& m) {
                            if (s >= m.sets.Size() || a >= m.sets[s].actions.Size()) { return; }
                            din::Action& act = m.sets[s].actions[a];
                            act.kind = static_cast<din::ActionKind>(
                                (static_cast<u8>(act.kind) + 1u) % 3u);
                        });
                    });
                    MakeButton(*row, detail::InteractionName(action.interaction.kind), 80.0f,
                               [self, s, a]() {
                        self->Mutate([s, a](din::InputMap& m) {
                            if (s >= m.sets.Size() || a >= m.sets[s].actions.Size()) { return; }
                            din::Action& act = m.sets[s].actions[a];
                            act.interaction.kind = static_cast<din::InteractionKind>(
                                (static_cast<u8>(act.interaction.kind) + 1u) % 4u);
                        });
                    });
                    MakeButton(*row, u8"+ Binding", 74.0f, [self, s, a]() {
                        self->Mutate([s, a](din::InputMap& m) {
                            if (s >= m.sets.Size() || a >= m.sets[s].actions.Size()) { return; }
                            din::Action& act = m.sets[s].actions[a];
                            din::Binding fresh;
                            if (act.kind == din::ActionKind::Axis2D)
                            {
                                fresh.source = din::BindingSource::GamepadStick;
                            }
                            act.bindings.PushBack(fresh);
                        });
                    });
                    MakeButton(*row, u8"x", 22.0f, [self, s, a]() {
                        self->Mutate([s, a](din::InputMap& m) {
                            if (s < m.sets.Size() && a < m.sets[s].actions.Size())
                            {
                                m.sets[s].actions.RemoveAt(a);
                            }
                        });
                    });

                    for (usize b = 0; b < action.bindings.Size(); ++b)
                    {
                        auto bindingRow = MakeRow(40.0f, 22.0f);
                        const bool isListening = m_listening && m_listenSet == s
                                              && m_listenAction == a && m_listenBinding == b;
                        AddLabel(*bindingRow,
                                 isListening ? StringView(u8"<press an input...>")
                                             : detail::DescribeBinding(action.bindings[b]).AsView(),
                                 1.0f);
                        MakeButton(*bindingRow, u8"Listen", 54.0f, [self, s, a, b]() {
                            self->BeginListen(s, a, b);
                        });
                        MakeButton(*bindingRow, u8"x", 22.0f, [self, s, a, b]() {
                            self->Mutate([s, a, b](din::InputMap& m) {
                                if (s >= m.sets.Size() || a >= m.sets[s].actions.Size()) { return; }
                                auto& bindings = m.sets[s].actions[a].bindings;
                                if (b < bindings.Size()) { bindings.RemoveAt(b); }
                            });
                        });
                    }
                }
            }

            auto footer = MakeRow(0.0f, 26.0f);
            MakeButton(*footer, u8"+ Add Set", 90.0f, [self]() {
                self->Mutate([](din::InputMap& m) {
                    din::ActionSet set;
                    set.name = String(u8"NewSet");
                    m.sets.PushBack(static_cast<din::ActionSet&&>(set));
                });
            });

            RefreshStatus();
            m_content->Invalidate();
        }

        void BeginListen(usize set, usize action, usize binding)
        {
            m_listening = true;
            m_listenSet = set;
            m_listenAction = action;
            m_listenBinding = binding;
            // Filter by the action's declared kind: a Button rebind ignores stick noise,
            // an Axis2D rebind captures sticks only.
            din::CaptureFilter filter;
            if (set < m_map.sets.Size() && action < m_map.sets[set].actions.Size())
            {
                switch (m_map.sets[set].actions[action].kind)
                {
                    case din::ActionKind::Button: break;   // keys + mouse + pad buttons
                    case din::ActionKind::Axis1D:
                        filter.gamepadAxes = true;
                        break;
                    case din::ActionKind::Axis2D:
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

        void RefreshStatus()
        {
            if (m_listening)
            {
                m_status->SetText(u8"Listening... press the new input (Esc cancels)");
                return;
            }
            String error;
            if (!din::ValidateInputMap(m_map, &error))
            {
                String message(u8"Invalid: ");
                message += error;
                m_status->SetText(message.AsView());
            }
            else
            {
                m_status->SetText(u8"Sets > actions > bindings. Click names to rename; Listen rebinds.");
            }
        }

        EditorContext* m_context = nullptr;
        String m_title;
        din::InputMap m_map;

        RefPtr<iui::View> m_content;
        RefPtr<iui::ScrollView> m_scroll;
        RefPtr<iui::FlexLayout> m_rows;
        RefPtr<iui::Label> m_status;

        bool m_listening = false;
        usize m_listenSet = 0;
        usize m_listenAction = 0;
        usize m_listenBinding = 0;
        din::CaptureFilter m_listenFilter;
    };

    class InputMapPageFactory final : public IEditorPageFactory
    {
    public:
        explicit InputMapPageFactory(irt::IApplicationHost& host) : m_host(&host) {}
        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &din::InputMapAsset::StaticType();
        }
        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       draconic::content::Instance& instance) override
        {
            auto* page = DefaultAllocator().New<InputMapEditorPage>(context, *m_host, instance);
            return UniquePtr<EditorPage>(page, DefaultAllocator());
        }

    private:
        irt::IApplicationHost* m_host;
    };

    /// The editor executable's entry point for the input plugin.
    inline void RegisterInputEditor(EditorContext& context, irt::IApplicationHost& host)
    {
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<InputMapPageFactory>(host), DefaultAllocator()));
    }

}
