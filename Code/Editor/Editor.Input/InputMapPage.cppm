// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Input - the `editor.input` module.
//
// InputMapPage: the editing surface for InputMapAsset - a scrollable
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
        // Binding + enum labels now live in foundation.input (:binding_names); re-exported here so
        // this page and its impl keep calling them as detail::<name>.
        using input::DescribeBinding;
        using input::InteractionName;
        using input::KeyName;
        using input::KindName;
        using input::PadButtonName;
        using input::SourceName;
        using input::ValidSources;
    }

    /// The input map editor's headless edits: what the rows' clicks and the listen capture do
    /// to the map, without a page. Every lookup is guarded (an out-of-range set, action or
    /// binding is a no-op or null), since a row can outlive the map it was built from.
    namespace input_map_edit
    {
        [[nodiscard]] inline input::ActionSet* SetAt(input::InputMap& map, usize set)
        {
            return set < map.sets.Size() ? &map.sets[set] : nullptr;
        }
        [[nodiscard]] inline input::Action* ActionAt(input::InputMap& map, usize set, usize action)
        {
            input::ActionSet* s = SetAt(map, set);
            return (s != nullptr && action < s->actions.Size()) ? &s->actions[action] : nullptr;
        }
        [[nodiscard]] inline bool HasBinding(input::InputMap& map, usize set, usize action,
                                             usize binding)
        {
            const input::Action* a = ActionAt(map, set, action);
            return a != nullptr && binding < a->bindings.Size();
        }
        /// A new binding for an action of `kind`: a stick for a 2D axis, else a key.
        [[nodiscard]] inline input::Binding FreshBinding(input::ActionKind kind)
        {
            input::Binding fresh;
            if (kind == input::ActionKind::Axis2D)
            {
                fresh.source = input::BindingSource::GamepadStick;
            }
            return fresh;
        }
        /// The next valid source for the kind after `current`'s (wrapping), on a FRESH binding:
        /// a source change resets the source-specific fields.
        [[nodiscard]] inline input::Binding CycleSource(input::ActionKind kind,
                                                        const input::Binding& current)
        {
            input::BindingSource valid[8];
            const usize n = input::ValidSources(kind, valid);
            usize index = 0;
            for (usize i = 0; i < n; ++i)
            {
                if (valid[i] == current.source)
                {
                    index = i;
                    break;
                }
            }
            input::Binding fresh;
            fresh.source = valid[(index + 1) % n];
            return fresh;
        }
        /// What a listen for an action of `kind` captures: a Button rebind ignores stick
        /// noise, an Axis2D rebind captures sticks only; one Composite2D DIRECTION is always a
        /// single key.
        [[nodiscard]] inline input::CaptureFilter FilterFor(input::ActionKind kind,
                                                            bool compositeDirection)
        {
            input::CaptureFilter filter;
            if (compositeDirection)
            {
                filter.mouseButtons = false;
                filter.gamepadButtons = false;
                return filter;
            }
            switch (kind)
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
            return filter;
        }
        /// Lands a capture: the whole binding, or (direction 0..3 = -X +X -Y +Y) just the
        /// captured KEY code into that Composite2D slot. Out-of-range targets are ignored.
        inline void ApplyCapture(input::InputMap& map, usize set, usize action, usize binding,
                                 i32 direction, const input::Binding& captured)
        {
            input::Action* a = ActionAt(map, set, action);
            if (a == nullptr || binding >= a->bindings.Size())
            {
                return;
            }
            input::Binding& target = a->bindings[binding];
            if (direction < 0)
            {
                target = captured;
                return;
            }
            u32* slot = direction == 0   ? &target.negX
                        : direction == 1 ? &target.posX
                        : direction == 2 ? &target.negY
                                         : &target.posY;
            *slot = captured.code;
        }
    }

    class InputMapEditorPage final : public app::UIEditorPage
    {
    public:
        InputMapEditorPage(EditorContext& context, runtime::IApplicationHost&,
                           foundation::content::Instance& instance)
            : app::UIEditorPage(context.Allocator()),
              m_context(&context), m_title(instance.Name())
        {
            RefPtr<ISerializable> object = instance.ReadObject();
            if (auto* asset = Cast<pipeline::InputMapAsset>(object.Get()))
            {
                m_map = asset->Map();
            }

            auto column = MakeRef<ui::FlexLayout>(Allocator());
            column->Direction = ui::Orientation::Vertical;
            column->Padding = ui::Thickness{8, 6};

            m_status = MakeRef<ui::Label>(Allocator(), StringView(u8""));
            m_status->FontSize.SetValue(12.0f);
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(20));
                column->AddView(m_status.Get(), lp);
            }

            m_scroll = MakeRef<ui::ScrollView>(Allocator());
            m_rows = MakeRef<ui::FlexLayout>(Allocator());
            m_rows->Direction = ui::Orientation::Vertical;
            m_rows->Spacing = 2.0f;
            m_scroll->AddView(m_rows.Get());
            {
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.FlexGrow = 1.0f;
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
                Allocator().New<MapEditCommand>(*this,
                                                       static_cast<input::InputMap&&>(before),
                                                       static_cast<input::InputMap&&>(after)),
                Allocator()));
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
            editor::EditorRootAllocator().New<InputMapPageFactory>(host), editor::EditorRootAllocator()));
    }

}
