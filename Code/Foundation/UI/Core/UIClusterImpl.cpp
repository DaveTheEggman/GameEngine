// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UI - module implementation unit for foundation.ui.
//
// Holds the two styling methods that call into View (StyleSelector::Matches, StyleSheet::Resolve).
// They cannot live in the :view interface partition without making :style_selector/:style_sheet depend
// back on :view (a module-partition cycle), and they cannot live in their own interface partitions
// without a forward-declared View being incomplete. An implementation unit sees the whole module
// (it implicitly imports the primary interface) and is outside the interface dependency graph.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

module foundation.ui;

using namespace foundation::core;

namespace foundation::ui
{
    // === View cluster method bodies (kept out of the :view interface partition to slim its BMI) ===

    void View::Invalidate()
    {
        m_needsRedraw = true;
        if (Context != nullptr)
        {
            Context->MarkNeedsRedraw();
        }
    }

    RootView* View::Root() const
    {
        View* view = const_cast<View*>(this);
        while (view != nullptr)
        {
            if (RootView* root = Cast<RootView>(view))
            {
                return root;
            }
            view = view->Parent;
        }
        return nullptr;
    }

    bool View::IsEffectivelyVisible() const
    {
        const View* v = this;
        while (v != nullptr)
        {
            if (v->Visibility != VisibilityValue::Visible)
            {
                return false;
            }
            if (Cast<RootView>(const_cast<View*>(v)) != nullptr)
            {
                return true;
            }
            v = v->Parent;
        }
        return false;
    }

    StyleValue View::ResolveStyle(StyleProperty prop)
    {
        if (m_inlineSheet)
        {
            StyleValue r = m_inlineSheet->Resolve(*this, prop);
            if (r.GetKind() != StyleValue::Kind::None)
            {
                return r;
            }
        }
        for (View* anc = this; anc != nullptr; anc = anc->Parent)
        {
            if (anc->m_localStyleSheet)
            {
                StyleValue r = anc->m_localStyleSheet->Resolve(*this, prop);
                if (r.GetKind() != StyleValue::Kind::None)
                {
                    return r;
                }
            }
        }
        if (Context != nullptr)
        {
            if (StyleSheet* ctxSheet = Context->GetStyleSheet())
            {
                StyleValue r = ctxSheet->Resolve(*this, prop);
                if (r.GetKind() != StyleValue::Kind::None)
                {
                    return r;
                }
            }
        }
        if (IsInheritableStyle(prop) && Parent != nullptr)
        {
            return Parent->ResolveStyle(prop);
        }
        return StyleValue::None();
    }

    String View::ResolveStyleFontFamily()
    {
        // Hold the StyleValue in a named local: AsString() borrows a view into it (dangles otherwise).
        StyleValue family = ResolveStyle(StyleProperty::FontFamily);
        if (Optional<StringView> s = family.AsString(); s.HasValue())
        {
            return String(s.Value());
        }
        if (Context != nullptr && Context->FontService() != nullptr)
        {
            return String(Context->FontService()->DefaultFontFamily());
        }
        return String{};
    }

    String View::ResolveStyleFontFamily(StringView instanceOverride)
    {
        if (instanceOverride.Size() > 0)
        {
            return String(instanceOverride);
        }
        return ResolveStyleFontFamily();
    }

    StyleValue View::ResolvePartStyle(StringView part, StyleProperty prop, ControlState partState)
    {
        if (m_inlineSheet)
        {
            StyleValue r = m_inlineSheet->ResolvePart(*this, part, prop, partState);
            if (r.GetKind() != StyleValue::Kind::None)
            {
                return r;
            }
        }
        for (View* anc = this; anc != nullptr; anc = anc->Parent)
        {
            if (anc->m_localStyleSheet)
            {
                StyleValue r = anc->m_localStyleSheet->ResolvePart(*this, part, prop, partState);
                if (r.GetKind() != StyleValue::Kind::None)
                {
                    return r;
                }
            }
        }
        if (Context != nullptr)
        {
            if (StyleSheet* ctxSheet = Context->GetStyleSheet())
            {
                StyleValue r = ctxSheet->ResolvePart(*this, part, prop, partState);
                if (r.GetKind() != StyleValue::Kind::None)
                {
                    return r;
                }
            }
        }
        return StyleValue::None();
    }

    void View::QueueRemove()
    {
        if (Context == nullptr || IsPendingDeletion)
        {
            return;
        }
        IsPendingDeletion = true;
        // Strong capture: the queue co-owns the view until the action drains, so a synchronous
        // RemoveView between queue and drain can never leave the lambda holding a freed pointer,
        // and the flag reset below is always safe (the ref drops at lambda end).
        RefPtr<View> self(this);
        Context->MutationQueueRef().QueueAction(
            [self]()
            {
                if (self->Parent != nullptr)
                {
                    if (ViewGroup* pg = Cast<ViewGroup>(self->Parent))
                    {
                        pg->RemoveView(self.Get(), false);
                    }
                }
                self->IsPendingDeletion = false;
            });
    }

    void View::QueueDestroy()
    {
        if (Context == nullptr || IsPendingDeletion)
        {
            return;
        }
        IsPendingDeletion = true;
        RefPtr<View> self(this); // strong capture (see QueueRemove) - also makes the reset safe
        Context->MutationQueueRef().QueueAction(
            [self]()
            {
                if (self->Parent != nullptr)
                {
                    if (ViewGroup* pg = Cast<ViewGroup>(self->Parent))
                    {
                        pg->RemoveView(self.Get(), true);
                    }
                }
                // Reset so a view kept alive by an outside ref (destroy is advisory under RefPtr
                // ownership) is not permanently immune to future queued removals.
                self->IsPendingDeletion = false;
            });
    }

    f32 ViewGroup::RootDpiScale(RootView* root) { return root != nullptr ? root->DpiScale : 1.0f; }

    // The base measure template method. Lives here because the dpi query
    // needs RootView complete. See the declaration comment in View.cppm for the contract.
    void View::Measure(BoxConstraints c)
    {
        const BoxMetrics metrics = ResolveBoxMetrics();
        BoxConstraints box = c.Deflate(metrics.Margin);

        const SizeSpec widthSpec = LayoutParams ? LayoutParams->Width : SizeSpec::Wrap();
        const SizeSpec heightSpec = LayoutParams ? LayoutParams->Height : SizeSpec::Wrap();
        if (widthSpec.kind == SizeSpec::Kind::Fixed || heightSpec.kind == SizeSpec::Kind::Fixed)
        {
            RootView* root = Root();
            const f32 dpiScale = (root != nullptr) ? Max(root->DpiScale, 0.01f) : 1.0f;
            if (widthSpec.kind == SizeSpec::Kind::Fixed)
            {
                const f32 w = widthSpec.ResolveFixed(dpiScale);
                box.MinWidth = w;
                box.MaxWidth = w;
            }
            if (heightSpec.kind == SizeSpec::Kind::Fixed)
            {
                const f32 h = heightSpec.ResolveFixed(dpiScale);
                box.MinHeight = h;
                box.MaxHeight = h;
            }
        }

        const Thickness chrome = metrics.Chrome();
        const Float2 content = OnMeasureContent(box.Deflate(chrome));
        if (content.x >= 0.0f)
        {
            MeasuredSize = Float2{box.ConstrainWidth(content.x + chrome.TotalHorizontal()),
                                  box.ConstrainHeight(content.y + chrome.TotalVertical())};
            return;
        }
        OnMeasure(box); // legacy seam - the control handles its own chrome
    }

    // The base arrange (margin inset + device-grid rounding). Rounds
    // EDGES independently (x and x+w each snap, width = snapped difference) so adjacent
    // rounded boxes stay gapless; local-grid rounding composes to the global grid because
    // every ancestor rounds too (integer sums stay integers, at fractional scales multiples
    // of 1/dpi stay multiples).
    void View::Layout(f32 x, f32 y, f32 width, f32 height)
    {
        const Thickness margin = LayoutParams ? LayoutParams->Margin : Thickness{};
        f32 bx = x + margin.Left;
        f32 by = y + margin.Top;
        f32 bw = Max(0.0f, width - margin.TotalHorizontal());
        f32 bh = Max(0.0f, height - margin.TotalVertical());

        RootView* root = Root();
        const f32 dpi = (root != nullptr) ? Max(root->DpiScale, 0.01f) : 1.0f;
        const f32 x1 = Round((bx + bw) * dpi) / dpi;
        const f32 y1 = Round((by + bh) * dpi) / dpi;
        bx = Round(bx * dpi) / dpi;
        by = Round(by * dpi) / dpi;
        bw = Max(0.0f, x1 - bx);
        bh = Max(0.0f, y1 - by);

        Bounds = Rectangle{bx, by, bw, bh};
        OnLayout(bx, by, bw, bh);
    }

    // Lazily create the RootView's PopupLayer (kept as the last child). Defined here because the
    // :popup_layer type is incomplete in the :view partition (module cycle) but complete in this impl unit.
    PopupLayer* RootView::GetPopupLayer()
    {
        if (!m_popupLayer)
        {
            RefPtr<PopupLayer> pl = MakeRef<PopupLayer>(DefaultAllocator());
            m_popupLayer = RefPtr<ViewGroup>(pl.Get()); // upcast + ref
            ViewGroup::AddView(pl.Get()); // base add (bypasses RootView's keep-last override)
        }
        return Cast<PopupLayer>(m_popupLayer.Get());
    }

    ViewGroup* ViewGroup::AddView(View* child, LayoutParamsPtr lp)
    {
        // Mutating the tree while it is being DRAWN corrupts the in-progress child walk (layout-
        // phase mutation is legitimate - virtualization realizes rows there). Defer draw-phase
        // mutations via UIContext::MutationQueueRef().QueueAction.
        DIAGNOSTIC_ASSERT(Context == nullptr || Context->CurrentPhase() != UIContext::Phase::Drawing);
        if (child == nullptr || child == this)
        {
            return this;
        }
        for (const RefPtr<View>& c : m_children)
        {
            if (c.Get() == child)
            {
                return this;
            }
        }

        if (child->Parent != nullptr)
        {
            if (ViewGroup* oldParent = Cast<ViewGroup>(child->Parent))
            {
                oldParent->RemoveView(child, false);
            }
        }

        if (lp)
        {
            child->LayoutParams = Move(lp);
        }
        else if (!child->LayoutParams)
        {
            child->LayoutParams = CreateDefaultLayoutParams();
        }

        child->Parent = this;
        if (Context != nullptr)
        {
            Context->AttachView(child);
        }
        else
        {
            child->Context = nullptr;
        }
        m_children.PushBack(RefPtr<View>(child));
        Invalidate();
        return this;
    }

    ViewGroup::~ViewGroup()
    {
        // A destroyed group must not leave a child pointing back at it. RemoveView clears the
        // back-pointer for children it detaches, but on destruction the children array is simply
        // released - so a child still held elsewhere (e.g. a persistent editor view reused
        // across PropertyGrid rebuilds) would keep a dangling Parent, and the next AddView would
        // dereference freed memory when it tries to detach from the old parent. Null it here.
        // ALSO detach from the context, mirroring RemoveView/RemoveAllViews: a still-attached
        // subtree destroyed by dropping its owning RefPtr would otherwise leave the UIContext
        // registry and the focus/hover/animation tables holding freed View*.
        for (const RefPtr<View>& child : m_children)
        {
            if (!child)
            {
                continue;
            }
            if (child->Context != nullptr)
            {
                child->Context->DetachView(child.Get());
            }
            if (child->Parent == this)
            {
                child->Parent = nullptr;
            }
        }
    }

    void ViewGroup::RemoveView(View* child, bool deleteChild)
    {
        (void)deleteChild; // RefPtr ownership makes this advisory: dropping the tree ref frees it.
        DIAGNOSTIC_ASSERT(Context == nullptr ||
                          Context->CurrentPhase() != UIContext::Phase::Drawing); // defer via MutationQueue
        if (child == nullptr)
        {
            return;
        }
        for (usize i = 0; i < m_children.Size(); ++i)
        {
            if (m_children[i].Get() != child)
            {
                continue;
            }
            RefPtr<View> keepAlive = m_children[i]; // hold across Detach + Parent clear
            if (child->Context != nullptr)
            {
                child->Context->DetachView(child);
            }
            child->Parent = nullptr;
            m_children.RemoveAt(i);
            Invalidate();
            return;
        }
    }

    void ViewGroup::RemoveAllViews(bool deleteChildren)
    {
        (void)deleteChildren;
        DIAGNOSTIC_ASSERT(Context == nullptr ||
                          Context->CurrentPhase() != UIContext::Phase::Drawing); // defer via MutationQueue
        for (const RefPtr<View>& child : m_children)
        {
            if (child->Context != nullptr)
            {
                child->Context->DetachView(child.Get());
            }
            child->Parent = nullptr;
        }
        m_children.Clear();
        Invalidate();
    }

    void ViewGroup::InsertView(View* child, usize index, LayoutParamsPtr lp)
    {
        DIAGNOSTIC_ASSERT(Context == nullptr ||
                          Context->CurrentPhase() != UIContext::Phase::Drawing); // defer via MutationQueue
        if (child == nullptr || child == this)
        {
            return;
        }
        for (const RefPtr<View>& c : m_children)
        {
            if (c.Get() == child)
            {
                return;
            }
        }

        if (child->Parent != nullptr)
        {
            if (ViewGroup* oldParent = Cast<ViewGroup>(child->Parent))
            {
                oldParent->RemoveView(child, false);
            }
        }

        if (lp)
        {
            child->LayoutParams = Move(lp);
        }
        else if (!child->LayoutParams)
        {
            child->LayoutParams = CreateDefaultLayoutParams();
        }

        child->Parent = this;
        if (Context != nullptr)
        {
            Context->AttachView(child);
        }
        else
        {
            child->Context = nullptr;
        }

        const usize clamped = index > m_children.Size() ? m_children.Size() : index;
        m_children.Insert(clamped, RefPtr<View>(child));
        Invalidate();
    }

    void ViewGroup::MoveView(View* child, usize index)
    {
        if (child == nullptr || m_children.IsEmpty())
        {
            return;
        }
        usize current = m_children.Size();
        for (usize i = 0; i < m_children.Size(); ++i)
        {
            if (m_children[i].Get() == child)
            {
                current = i;
                break;
            }
        }
        if (current == m_children.Size())
        {
            return;
        } // not a child of this group
        const usize target = index >= m_children.Size() ? m_children.Size() - 1 : index;
        if (target == current)
        {
            return;
        }
        RefPtr<View> keepAlive = m_children[current];
        m_children.RemoveAt(current);
        // Inserting at `target` after the removal lands the child at final index `target`
        // regardless of direction (the removal already shifted trailing entries left).
        m_children.Insert(target, Move(keepAlive));
        Invalidate();
    }

    void MutationQueue::QueueDelete(View* view)
    {
        if (view == nullptr || view->IsPendingDeletion)
        {
            return;
        }
        view->IsPendingDeletion = true;
        RefPtr<View> keep(view); // strong capture (see View::QueueRemove) - reset is then safe
        QueueAction(
            [keep]()
            {
                if (keep->Parent != nullptr)
                {
                    if (ViewGroup* pg = Cast<ViewGroup>(keep->Parent))
                    {
                        pg->RemoveView(keep.Get(), false);
                    }
                }
                keep->IsPendingDeletion = false;
            });
    }

    bool StyleSelector::Matches(const View& view, ControlState state,
                                StringView pseudoElement) const
    {
        if (ViewType != nullptr && !IsDerivedFrom(view.GetType(), ViewType))
        {
            return false;
        }
        for (const String& cls : StyleClasses)
        {
            if (!view.HasClass(cls.AsView()))
            {
                return false;
            }
        }
        if (State.HasValue())
        {
            const ControlState required = State.Value();
            if (required != ControlState::Normal && !HasFlag(state, required))
            {
                return false;
            }
        }
        if (PseudoElement.HasValue())
        {
            if (pseudoElement.Size() == 0u || PseudoElement.Value().AsView() != pseudoElement)
            {
                return false;
            }
        }
        else if (pseudoElement.Size() != 0u)
        {
            return false;
        }
        return true;
    }

    StyleValue StyleSheet::Resolve(const View& view, StyleProperty prop) const
    {
        return ResolveMatching(view, view.GetControlState(), StringView{}, prop);
    }
}
