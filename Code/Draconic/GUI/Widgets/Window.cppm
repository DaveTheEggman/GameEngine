// Draconic GUI - :window partition
//
// Window: a movable, resizable panel with a title bar and a content area. Modeled on eepp's
// UIWindow (role only). Dragging the title bar moves the window; dragging the bottom-right
// grip resizes it (clamped to a minimum). Pressing anywhere on the title raises the window to
// the front. Movement uses incremental world-space cursor deltas via an internal DragHandle
// that follows the Slider/ScrollBar capture discipline (own m_dragging flag, so a drag that
// leaves the handle keeps tracking). Nested parent transforms are not accounted for (deltas
// are treated as parent-local) - fine for top-level windows under an untransformed root.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:window;

import draconic.core;   // RefPtr, MakeRef, Function, Move, Max, Float2
import draconic.fonts;  // CachedFont
import :rect;
import :event;
import :drawable;
import :rectangle_drawable;
import :node;
import :label;
import :ui_widget;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

export namespace draconic::gui
{
    // A widget that reports incremental world-space cursor deltas while dragged. Internal to
    // Window (title bar + resize grip), but generally reusable.
    class DragHandle : public UIWidget
    {
        DRACONIC_OBJECT(DragHandle, UIWidget)
    public:
        core::Function<void(core::Float2)> OnDrag; // incremental cursor delta
        core::Function<void()> OnPressed;

    protected:
        void OnMouseDown(const MouseEvent& event) override
        {
            UINode::OnMouseDown(event);
            if (OnPressed) OnPressed();               // raise on any button
            if (event.Button != MouseButton::Left) return; // but only the left button drags
            m_dragging = true;
            m_last = event.Position;
        }
        void OnMouseMove(const MouseEvent& event) override
        {
            if (!m_dragging) return;
            const core::Float2 delta{ event.Position.x - m_last.x, event.Position.y - m_last.y };
            m_last = event.Position;
            if (OnDrag) OnDrag(delta);
        }
        void OnMouseUp(const MouseEvent& event) override { m_dragging = false; UINode::OnMouseUp(event); }

    private:
        bool m_dragging = false;
        core::Float2 m_last{ 0.0f, 0.0f };
    };

    class Window : public UIWidget
    {
        DRACONIC_OBJECT(Window, UIWidget)
    public:
        Window()
        {
            SetTag(core::StringView(u8"window"));
            SetClipChildren(true);
            SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), m_bodyColor));

            m_titleBar = core::MakeRef<DragHandle>(core::DefaultAllocator());
            m_titleBar->SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), m_titleColor));
            Window* self = this;
            m_titleBar->OnDrag = [self](core::Float2 d) { self->SetPosition(core::Float2{ self->GetPosition().x + d.x, self->GetPosition().y + d.y }); };
            m_titleBar->OnPressed = [self]() { self->ToFront(); };
            AddChild(m_titleBar.Get());

            m_title = core::MakeRef<Label>(core::DefaultAllocator());
            m_title->SetTextAlignment(TextHAlign::Left, TextVAlign::Middle);
            m_title->SetPadding(Thickness{ 8.0f, 0.0f, 8.0f, 0.0f });
            m_title->SetHitTestVisible(false); // clicks fall through to the draggable title bar
            m_titleBar->AddChild(m_title.Get());

            m_contentHost = core::MakeRef<UIWidget>(core::DefaultAllocator());
            m_contentHost->SetClipChildren(true);
            AddChild(m_contentHost.Get());

            m_resizeGrip = core::MakeRef<DragHandle>(core::DefaultAllocator());
            m_resizeGrip->SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), m_gripColor));
            m_resizeGrip->OnDrag = [self](core::Float2 d) { self->ResizeBy(d); };
            m_resizeGrip->OnPressed = [self]() { self->ToFront(); };
            AddChild(m_resizeGrip.Get());
        }

        // The area for window content (add your widgets here).
        [[nodiscard]] Node* GetContent() const noexcept { return m_contentHost.Get(); }

        void SetTitle(core::StringView text) { m_title->SetText(text); }
        void SetFont(fonts::CachedFont* font) { m_title->SetFont(font); }
        void SetMinSize(core::Float2 size) { m_minSize = size; }
        void SetTitleBarHeight(f32 height) { m_titleBarHeight = core::Max(1.0f, height); Relayout(); }

        void SetBodyColor(Color color) { m_bodyColor = color; SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), color)); }
        void SetTitleColor(Color color) { m_titleColor = color; m_titleBar->SetBackground(core::MakeRef<RectangleDrawable>(core::DefaultAllocator(), color)); }

    protected:
        void OnSizeChange() override { Relayout(); }

    private:
        void ResizeBy(core::Float2 delta)
        {
            const core::Float2 s = GetSize();
            SetSize(core::Float2{ core::Max(m_minSize.x, s.x + delta.x), core::Max(m_minSize.y, s.y + delta.y) });
        }

        void Relayout()
        {
            const core::Float2 s = GetSize();
            m_titleBar->SetPosition(core::Float2{ 0.0f, 0.0f });
            m_titleBar->SetSize(core::Float2{ s.x, m_titleBarHeight });
            m_title->SetSize(core::Float2{ s.x, m_titleBarHeight });

            m_contentHost->SetPosition(core::Float2{ 0.0f, m_titleBarHeight });
            m_contentHost->SetSize(core::Float2{ s.x, core::Max(0.0f, s.y - m_titleBarHeight) });

            m_resizeGrip->SetSize(core::Float2{ m_gripSize, m_gripSize });
            m_resizeGrip->SetPosition(core::Float2{ s.x - m_gripSize, s.y - m_gripSize });
        }

        RefPtr<DragHandle> m_titleBar;
        RefPtr<Label> m_title;
        RefPtr<UIWidget> m_contentHost;
        RefPtr<DragHandle> m_resizeGrip;
        core::Float2 m_minSize{ 120.0f, 60.0f };
        f32 m_titleBarHeight = 28.0f;
        f32 m_gripSize = 14.0f;
        Color m_bodyColor{ 0.16f, 0.17f, 0.21f, 1.0f };
        Color m_titleColor{ 0.24f, 0.28f, 0.36f, 1.0f };
        Color m_gripColor{ 0.40f, 0.44f, 0.52f, 1.0f };
    };

    DRACONIC_DEFINE_OBJECT(DragHandle, "draconic::gui")
    DRACONIC_DEFINE_OBJECT(Window, "draconic::gui")
}
