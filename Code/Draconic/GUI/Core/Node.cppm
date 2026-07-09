// Draconic GUI - :node partition
//
// Node: the retained-mode scene-graph node - the tree content base (eepp Scene::Node,
// minus the drawing/matrix/clip, which reattaches when the render seam wires in). Ported
// from eepp include/eepp/scene/node.hpp, adapted to Draconic: Object + Transformable
// bases (Cast<T> downcasts); the raw-pointer intrusive sibling list becomes an owning
// Array<RefPtr<Node>> children with a non-owning parent back-pointer.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.gui:node;

import draconic.core;   // Object, RefPtr, Array, Move, Float2
import :rect;
import :transform2d;
import :transformable;
import :event;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    class Node : public Object, public Transformable
    {
        DRACONIC_OBJECT(Node, Object)
    public:
        static constexpr usize kInvalidIndex = static_cast<usize>(-1);

        Node() = default;

        // === Tree query ===
        [[nodiscard]] Node* GetParent() const noexcept { return m_parent; }
        [[nodiscard]] usize ChildCount() const noexcept { return m_children.Size(); }
        [[nodiscard]] Node* GetChildAt(usize index) const
        {
            return index < m_children.Size() ? m_children[index].Get() : nullptr;
        }
        [[nodiscard]] Node* GetFirstChild() const { return m_children.Size() ? m_children[0].Get() : nullptr; }
        [[nodiscard]] Node* GetLastChild() const
        {
            return m_children.Size() ? m_children[m_children.Size() - 1].Get() : nullptr;
        }
        [[nodiscard]] usize GetChildIndex(const Node* child) const
        {
            for (usize i = 0; i < m_children.Size(); ++i)
                if (m_children[i].Get() == child) return i;
            return kInvalidIndex;
        }
        [[nodiscard]] bool HasChild(const Node* child) const { return GetChildIndex(child) != kInvalidIndex; }

        [[nodiscard]] Node* GetNextSibling() const
        {
            if (m_parent == nullptr) return nullptr;
            const usize i = m_parent->GetChildIndex(this);
            return (i != kInvalidIndex && i + 1 < m_parent->m_children.Size())
                ? m_parent->m_children[i + 1].Get() : nullptr;
        }
        [[nodiscard]] Node* GetPrevSibling() const
        {
            if (m_parent == nullptr) return nullptr;
            const usize i = m_parent->GetChildIndex(this);
            return (i != kInvalidIndex && i > 0) ? m_parent->m_children[i - 1].Get() : nullptr;
        }

        // === Tree mutation === (owning: the tree holds a RefPtr to each child)
        virtual Node* AddChild(Node* child)
        {
            if (child == nullptr || child == this || child->m_parent == this) return this;
            RefPtr<Node> keepAlive(child);          // survive reparenting off the old parent
            if (child->m_parent != nullptr) child->m_parent->RemoveChild(child);
            child->m_parent = this;
            m_children.PushBack(core::Move(keepAlive));
            child->HandleParentChange();
            Invalidate();
            return this;
        }

        Node* AddChildAt(Node* child, usize index)
        {
            AddChild(child);
            SetChildIndex(child, index);
            return this;
        }

        void RemoveChild(Node* child)
        {
            const usize index = GetChildIndex(child);
            if (index == kInvalidIndex) return;
            RefPtr<Node> keepAlive = m_children[index]; // hold across detach + callback
            child->m_parent = nullptr;
            m_children.RemoveAt(index);
            child->HandleParentChange();
            Invalidate();
        }

        void RemoveAllChildren()
        {
            while (m_children.Size() != 0) RemoveChild(m_children[m_children.Size() - 1].Get());
        }

        void RemoveFromParent() { if (m_parent != nullptr) m_parent->RemoveChild(this); }

        // === Z-order (draw/hit order = child index; last = topmost) ===
        void SetChildIndex(Node* child, usize newIndex)
        {
            const usize cur = GetChildIndex(child);
            if (cur == kInvalidIndex) return;
            if (newIndex >= m_children.Size()) newIndex = m_children.Size() - 1;
            if (cur == newIndex) return;
            RefPtr<Node> ref = m_children[cur];
            m_children.RemoveAt(cur);
            m_children.Insert(newIndex, core::Move(ref));
            Invalidate();
        }
        void ToFront() { if (m_parent != nullptr) m_parent->SetChildIndex(this, m_parent->m_children.Size()); }
        void ToBack() { if (m_parent != nullptr) m_parent->SetChildIndex(this, 0); }

        // === Size / bounds ===
        void SetSize(core::Float2 size)
        {
            if (size == m_size) return;
            m_size = size;
            HandleSizeChange();
        }
        [[nodiscard]] core::Float2 GetSize() const noexcept { return m_size; }
        [[nodiscard]] Rect GetLocalBounds() const noexcept { return Rect{ 0.0f, 0.0f, m_size.x, m_size.y }; }

        // World transform accumulates the parent chain (parentWorld * local).
        [[nodiscard]] Transform2D GetWorldTransform() const
        {
            return m_parent != nullptr ? (m_parent->GetWorldTransform() * GetTransform()) : GetTransform();
        }
        [[nodiscard]] core::Float2 ConvertToWorldSpace(core::Float2 nodePoint) const
        {
            return GetWorldTransform().TransformPoint(nodePoint);
        }
        [[nodiscard]] core::Float2 ConvertToNodeSpace(core::Float2 worldPoint) const
        {
            return GetWorldTransform().GetInverse().TransformPoint(worldPoint);
        }
        [[nodiscard]] core::Float2 GetScreenPosition() const { return ConvertToWorldSpace(core::Float2{ 0.0f, 0.0f }); }
        [[nodiscard]] Rect GetScreenBounds() const { return GetWorldTransform().TransformRect(GetLocalBounds()); }

        // === Visibility / enabled / alpha ===
        void SetVisible(bool visible) { if (visible == m_visible) return; m_visible = visible; HandleVisibilityChange(); }
        [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
        [[nodiscard]] bool IsTreeVisible() const { return m_visible && (m_parent == nullptr || m_parent->IsTreeVisible()); }
        void SetEnabled(bool enabled) { if (enabled == m_enabled) return; m_enabled = enabled; HandleEnabledChange(); }
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
        void SetAlpha(f32 alpha) { m_alpha = alpha; Invalidate(); }
        [[nodiscard]] f32 GetAlpha() const noexcept { return m_alpha; }

        // === Hit testing ===
        [[nodiscard]] virtual bool PointInside(core::Float2 worldPoint) const
        {
            const core::Float2 p = ConvertToNodeSpace(worldPoint);
            return p.x >= 0.0f && p.x <= m_size.x && p.y >= 0.0f && p.y <= m_size.y;
        }
        // Topmost visible descendant (or self) under a world point; nullptr if none.
        [[nodiscard]] virtual Node* OverFind(core::Float2 worldPoint)
        {
            if (!m_visible) return nullptr;
            for (usize i = m_children.Size(); i-- > 0; )
                if (Node* hit = m_children[i]->OverFind(worldPoint)) return hit;
            return PointInside(worldPoint) ? this : nullptr;
        }

        // === Invalidation === (marks self + ancestors until an already-dirty one)
        void Invalidate()
        {
            m_needsRedraw = true;
            for (Node* p = m_parent; p != nullptr && !p->m_needsRedraw; p = p->m_parent)
                p->m_needsRedraw = true;
        }
        [[nodiscard]] bool NeedsRedraw() const noexcept { return m_needsRedraw; }
        void ClearNeedsRedraw() noexcept { m_needsRedraw = false; }

        // === Transformable overrides: geometry changes invalidate + notify ===
        void SetPosition(core::Float2 position) override { Transformable::SetPosition(position); HandlePositionChange(); }
        void SetRotation(f32 radians) override { Transformable::SetRotation(radians); Invalidate(); }
        void SetScale(core::Float2 factors) override { Transformable::SetScale(factors); Invalidate(); }

        // === Event listeners ===
        u32 AddEventListener(EventType type, EventCallback callback)
        {
            const u32 id = ++m_nextListenerId;
            m_listeners.PushBack(Listener{ id, type, core::Move(callback) });
            return id;
        }
        void RemoveEventListener(u32 id)
        {
            for (usize i = 0; i < m_listeners.Size(); ++i)
                if (m_listeners[i].Id == id) { m_listeners.RemoveAt(i); return; }
        }
        void SendEvent(const Event& event)
        {
            for (Listener& l : m_listeners)
                if (l.Type == event.Type) l.Callback(event);
        }

        // === Change hooks (subclass overrides; the Handle* wrappers fire the event too) ===
        virtual void OnPositionChange() {}
        virtual void OnSizeChange() {}
        virtual void OnVisibilityChange() {}
        virtual void OnEnabledChange() {}
        virtual void OnParentChange() {}

    protected:
        void HandlePositionChange() { OnPositionChange(); Invalidate(); SendEvent(Event(EventType::PositionChanged, this)); }
        void HandleSizeChange() { OnSizeChange(); Invalidate(); SendEvent(Event(EventType::SizeChanged, this)); }
        void HandleVisibilityChange() { OnVisibilityChange(); Invalidate(); SendEvent(Event(EventType::VisibilityChanged, this)); }
        void HandleEnabledChange() { OnEnabledChange(); Invalidate(); SendEvent(Event(EventType::EnabledChanged, this)); }
        void HandleParentChange() { OnParentChange(); Invalidate(); SendEvent(Event(EventType::ParentChanged, this)); }

        struct Listener
        {
            u32 Id;
            EventType Type;
            EventCallback Callback;
        };

        Node* m_parent = nullptr;            // non-owning back-pointer
        Array<RefPtr<Node>> m_children;      // owning
        Array<Listener> m_listeners;
        core::Float2 m_size{ 0.0f, 0.0f };
        f32 m_alpha = 1.0f;
        bool m_visible = true;
        bool m_enabled = true;
        bool m_needsRedraw = true;
        u32 m_nextListenerId = 0;
    };

    DRACONIC_DEFINE_OBJECT(Node, "draconic::gui")
}
