// UI.Gamekit - widget implementation unit: the game-UI widgets' RTTI definitions + logic (MenuList
// now; Bar / Ticker / Toast to follow). Kept out of the interface partitions (GCC gcm-cluster
// hygiene) and separate from GamekitImpl.cpp, which owns the ScreenStack + `<screen>` markup.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module foundation.ui.gamekit;

import foundation.core;
import foundation.ui;
import foundation.input; // ButtonPrompt: DescribeBinding + InputMap

using namespace foundation::core;

namespace foundation::ui::gamekit
{
    // ==================================================================================== MenuList ===

    RTTI_DEFINE_OBJECT(MenuList, "rtti::ui.gamekit")

    MenuList::MenuList()
    {
        Direction = foundation::ui::Orientation::Vertical;
        AlignItems = foundation::ui::Align::Stretch; // rows fill the menu's width
        Spacing = 4.0f;
        AddClass(u8"menu");
    }

    foundation::ui::Button* MenuList::AddItem(StringView label)
    {
        RefPtr<foundation::ui::Button> row =
            MakeRef<foundation::ui::Button>(DefaultAllocator(), label);
        row->AddClass(u8"menu-item");
        foundation::ui::Button* raw = row.Get();
        m_items.PushBack(raw);
        AddView(raw); // ViewGroup takes an owning ref (m_children); m_items keeps the raw pointer
        RewireFocusChain();
        return raw;
    }

    foundation::ui::Button* MenuList::AddItem(StringView label, Function<void()> onSelect)
    {
        foundation::ui::Button* row = AddItem(label);
        const i32 index = static_cast<i32>(m_items.Size() - 1);
        MenuList* self = this;
        row->OnClick.Add(
            [self, index, cb = Move(onSelect)](foundation::ui::ButtonBase*)
            {
                if (cb)
                {
                    cb();
                }
                self->OnItemActivated.Invoke(self, index);
            });
        return row;
    }

    void MenuList::ClearItems()
    {
        RemoveAllViews();
        m_items.Clear();
    }

    void MenuList::RewireFocusChain()
    {
        const usize n = m_items.Size();
        for (usize i = 0; i < n; ++i)
        {
            // Wrap: the first row's Up goes to the last, the last row's Down goes to the first.
            m_items[i]->NextFocusUp = m_items[(i + n - 1) % n]->Id;
            m_items[i]->NextFocusDown = m_items[(i + 1) % n]->Id;
        }
    }

    i32 MenuList::SelectedIndex() const
    {
        if (Context == nullptr)
        {
            return -1;
        }
        foundation::ui::FocusManager* fm = Context->GetFocusManager();
        if (fm == nullptr)
        {
            return -1;
        }
        const foundation::ui::View* focused = fm->FocusedView();
        for (usize i = 0; i < m_items.Size(); ++i)
        {
            if (m_items[i] == focused)
            {
                return static_cast<i32>(i);
            }
        }
        return -1;
    }

    void MenuList::SetSelectedIndex(i32 index)
    {
        if (index < 0 || static_cast<usize>(index) >= m_items.Size() || Context == nullptr)
        {
            return;
        }
        if (foundation::ui::FocusManager* fm = Context->GetFocusManager())
        {
            fm->SetFocus(m_items[static_cast<usize>(index)],
                         foundation::ui::FocusSource::Programmatic);
        }
    }

    void MenuList::FocusFirst()
    {
        SetSelectedIndex(m_items.IsEmpty() ? -1 : 0);
    }

    // ========================================================================================= Bar ===

    RTTI_DEFINE_OBJECT(Bar, "rtti::ui.gamekit")

    void Bar::AnimateTo(f32 target, f32 duration)
    {
        const f32 clamped = Max(0.0f, Min(target, 1.0f));
        foundation::ui::AnimationManager* am = (Context != nullptr) ? Context->Animations() : nullptr;
        if (am == nullptr || duration <= 0.0f)
        {
            Value.SetValue(clamped); // snap: no frame pump to animate against
            return;
        }
        am->CancelForView(this); // a new drain replaces the in-flight one (no stacking)
        Bar* self = this;
        UniquePtr<foundation::ui::Animation> anim = MakeUnique<foundation::ui::FloatAnimation>(
            DefaultAllocator(), Value.Value(), clamped, duration,
            Function<void(f32)>{[self](f32 v) { self->Value.SetValue(v); }});
        anim->SetTarget(this); // cancelled if the Bar is deleted mid-drain
        am->Add(Move(anim));
    }

    // ====================================================================================== Ticker ===

    RTTI_DEFINE_OBJECT(Ticker, "rtti::ui.gamekit")

    Ticker::Ticker() { Render(0); }

    void Ticker::Render(i64 value)
    {
        m_current = value;
        SetText(Format(u8"{}", value).AsView());
    }

    void Ticker::SetNumber(i64 value) { Render(value); }

    void Ticker::AnimateTo(i64 target, f32 duration)
    {
        foundation::ui::AnimationManager* am = (Context != nullptr) ? Context->Animations() : nullptr;
        if (am == nullptr || duration <= 0.0f)
        {
            Render(target); // snap
            return;
        }
        am->CancelForView(this);
        Ticker* self = this;
        const f32 from = static_cast<f32>(m_current);
        const f32 to = static_cast<f32>(target);
        UniquePtr<foundation::ui::Animation> anim = MakeUnique<foundation::ui::FloatAnimation>(
            DefaultAllocator(), from, to, duration, Function<void(f32)>{[self](f32 v)
                                                                        {
                                                                            // round to the nearest
                                                                            // whole number each frame
                                                                            const f32 r =
                                                                                v >= 0.0f ? v + 0.5f
                                                                                          : v - 0.5f;
                                                                            self->Render(
                                                                                static_cast<i64>(r));
                                                                        }});
        anim->SetTarget(this);
        // Write the exact target on completion (the f32 end-value may not round back to it).
        anim->OnComplete.Add([self, target](foundation::ui::Animation*) { self->Render(target); });
        am->Add(Move(anim));
    }

    // ================================================================================= ButtonPrompt ===

    RTTI_DEFINE_OBJECT(ButtonPrompt, "rtti::ui.gamekit")

    ButtonPrompt::ButtonPrompt()
    {
        Direction = foundation::ui::Orientation::Horizontal;
        AlignItems = foundation::ui::Align::Center;
        Spacing = 6.0f;
        AddClass(u8"button-prompt");

        RefPtr<foundation::ui::Label> cap = MakeRef<foundation::ui::Label>(DefaultAllocator());
        cap->AddClass(u8"keycap"); // the theme can draw a key-cap chip around it
        m_keycap = cap.Get();
        AddView(cap.Get());

        RefPtr<foundation::ui::Label> txt = MakeRef<foundation::ui::Label>(DefaultAllocator());
        m_text = txt.Get();
        AddView(txt.Get());
    }

    void ButtonPrompt::Set(StringView bindingLabel, StringView text)
    {
        m_keycap->SetText(Format(u8"[{}]", bindingLabel).AsView());
        m_text->SetText(text);
    }

    void ButtonPrompt::SetFromAction(const foundation::input::InputMap& map, StringView actionName,
                                     StringView text)
    {
        String resolved;
        for (const foundation::input::ActionSet& set : map.sets)
        {
            for (const foundation::input::Action& action : set.actions)
            {
                if (action.name.AsView() == actionName && !action.bindings.IsEmpty())
                {
                    resolved = foundation::input::DescribeBinding(action.bindings[0]);
                    break;
                }
            }
            if (!resolved.IsEmpty())
            {
                break;
            }
        }
        Set(resolved.IsEmpty() ? StringView(u8"-") : resolved.AsView(), text);
    }
}
