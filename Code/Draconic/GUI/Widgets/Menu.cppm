// Draconic GUI - :menu partition
//
// Menu (PopupMenu): a floating vertical list of activatable items - a context menu or a menu
// dropped from a menu bar. Modeled on eepp's UIPopUpMenu (role only). Open() shows it at a
// position as a top-level popup (over everything) via the EventDispatcher's popup support, so
// an outside click or Escape dismisses it; activating an item runs its action and closes the
// menu. Submenus, separators, icons, and checkable items are follow-ups.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:menu;

import draconic.core;   // RefPtr, MakeRef, Array, Function, Move, Max
import draconic.fonts;  // CachedFont
import :rect;
import :event;
import :draw_context;
import :rectangle_drawable;
import :node;
import :label;
import :ui_widget;
import :event_dispatcher;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

export namespace draconic::gui
{
    // One activatable row. Highlights on hover; clicking runs its picked callback.
    class MenuItem : public Label
    {
        DRACONIC_OBJECT(MenuItem, Label)
    public:
        MenuItem()
        {
            SetTextAlignment(TextHAlign::Left, TextVAlign::Middle);
            SetPadding(Thickness{ 12.0f, 0.0f, 12.0f, 0.0f });
        }

        void SetOnPicked(core::Function<void()> callback) { m_onPicked = core::Move(callback); }
        void SetHighlightColor(Color color) { m_highlight = color; }

    protected:
        void OnMouseClick(const MouseEvent&) override { if (m_onPicked) m_onPicked(); }

        void OnDraw(DrawContext& ctx, const Rect& localBounds) override
        {
            if (IsHovered())
                ctx.VG().FillRect(GetLocalBounds().ToRectangle(), m_highlight);
            Label::OnDraw(ctx, localBounds);
        }

    private:
        Color m_highlight{ 0.24f, 0.40f, 0.62f, 1.0f };
        core::Function<void()> m_onPicked;
    };

    class Menu : public UIWidget
    {
        DRACONIC_OBJECT(Menu, UIWidget)
    public:
        Menu()
        {
            SetTag(core::StringView(u8"menu"));
            SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), m_panelColor));
        }

        void SetFont(fonts::CachedFont* font) { m_font = font; for (MenuItem* it : m_items) it->SetFont(font); }
        void SetWidth(f32 width) { m_width = core::Max(1.0f, width); Relayout(); }
        void SetItemHeight(f32 height) { m_itemHeight = core::Max(1.0f, height); Relayout(); }

        // Add an item with its action. Activating it runs `action`, then closes the menu.
        void AddItem(core::StringView text, core::Function<void()> action)
        {
            auto item = core::MakeRef<MenuItem>(core::DefaultAllocator());
            item->SetText(text);
            item->SetFont(m_font);
            item->SetTextColor(m_textColor);
            Menu* self = this;
            core::Function<void()> act = core::Move(action);
            item->SetOnPicked([self, act = core::Move(act)]() { if (act) act(); self->CloseSelf(); });
            AddChild(item.Get());
            m_items.PushBack(item.Get());
            Relayout();
        }

        [[nodiscard]] usize ItemCount() const noexcept { return m_items.Size(); }
        [[nodiscard]] bool IsOpen() const noexcept { return m_open; }

        // Show the menu at `position` (root-local) as a popup, attached under `owner`'s root.
        void Open(Node& owner, core::Float2 position)
        {
            Node* root = owner.GetRootNode();
            EventDispatcher* dispatcher = owner.GetEventDispatcher();
            if (root == nullptr || dispatcher == nullptr) return;
            SetPosition(position);
            root->AddChild(this);
            m_open = true;
            Menu* self = this;
            dispatcher->OpenPopup(this, &owner, [self]() { self->OnClosed(); });
        }

    protected:
        void OnSizeChange() override { Relayout(); }

    private:
        void Relayout()
        {
            for (usize i = 0; i < m_items.Size(); ++i)
            {
                m_items[i]->SetPosition(core::Float2{ 0.0f, static_cast<f32>(i) * m_itemHeight });
                m_items[i]->SetSize(core::Float2{ m_width, m_itemHeight });
            }
            SetSize(core::Float2{ m_width, static_cast<f32>(m_items.Size()) * m_itemHeight });
        }

        void CloseSelf() { if (EventDispatcher* d = GetEventDispatcher()) d->ClosePopup(); else OnClosed(); }
        void OnClosed() { m_open = false; RemoveFromParent(); }

        Array<MenuItem*> m_items; // owned as children
        fonts::CachedFont* m_font = nullptr;
        bool m_open = false;
        f32 m_width = 160.0f;
        f32 m_itemHeight = 26.0f;
        Color m_panelColor{ 0.16f, 0.17f, 0.21f, 1.0f };
        Color m_textColor{ 0.88f, 0.90f, 0.94f, 1.0f };
    };

    DRACONIC_DEFINE_OBJECT(MenuItem, "draconic::gui")
    DRACONIC_DEFINE_OBJECT(Menu, "draconic::gui")
}
