// UI.Gamekit - :button_prompt partition
//
// ButtonPrompt: a device hint - "[E] Deliver" - pairing a binding "keycap" with an action label.
// Set() takes a raw binding label (pure display, no input dependency). SetFromAction() resolves an
// action's FIRST binding from an InputMap through foundation.input's DescribeBinding and shows it.
//
// Deferred (needs input-side work): active-device switching ("[A]" on a pad vs "[E]" on keyboard)
// and pad-button GLYPHS. This first cut shows the first binding as text, which is correct for a
// single-scheme game and a fine default otherwise.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module foundation.ui.gamekit:button_prompt;

import foundation.core;
import foundation.ui;
import foundation.input;

using namespace foundation::core;

export namespace foundation::ui::gamekit
{
    class ButtonPrompt : public foundation::ui::FlexLayout
    {
        RTTI_OBJECT(ButtonPrompt, foundation::ui::FlexLayout)
    public:
        ButtonPrompt();

        // Show a raw binding label + action text: a "[label]" keycap followed by `text`.
        void Set(StringView bindingLabel, StringView text);

        // Resolve `actionName`'s FIRST binding from `map` (searching all sets) via DescribeBinding and
        // show it with `text`. If the action is missing or unbound, the keycap shows "-".
        void SetFromAction(const foundation::input::InputMap& map, StringView actionName,
                           StringView text);

        [[nodiscard]] foundation::ui::Label* Keycap() const noexcept { return m_keycap; }
        [[nodiscard]] foundation::ui::Label* TextLabel() const noexcept { return m_text; }

    private:
        foundation::ui::Label* m_keycap = nullptr; // the "[E]" chip
        foundation::ui::Label* m_text = nullptr;   // the action description
    };
}
