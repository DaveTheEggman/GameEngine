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
        // The safe default: any mutation may have changed geometry, so the host re-lays-out
        // before redrawing. Visual-only producers use InvalidateVisual to skip that pass.
        m_needsRedraw = true;
        if (Context != nullptr)
        {
            Context->MarkNeedsLayout();
        }
    }

    void View::InvalidateVisual()
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

    void View::InvalidateStyle()
    {
        m_styleCache.valid = false;
        if (Context != nullptr)
        {
            Context->InvalidateStyles();
        }
        Invalidate();
    }

    const View::StyleCache& View::EnsureStyleCache()
    {
        const ControlState state = GetControlState();
        const u32 generation = Context != nullptr ? Context->StyleGeneration() : 0u;
        // The chain: the sheets whose versions this cache keys on, and the local sheets it
        // collects from (OUTERMOST ancestor first, so nearer wins later in the list).
        View* chain[64];
        usize depth = 0;
        u32 chainVersion = 0;
        if (Context != nullptr)
        {
            if (const StyleSheet* sheet = Context->GetStyleSheet())
            {
                chainVersion += sheet->Version();
            }
        }
        for (View* anc = this; anc != nullptr && depth < 64; anc = anc->Parent)
        {
            if (anc->m_localStyleSheet)
            {
                chain[depth++] = anc;
                chainVersion += anc->m_localStyleSheet->Version();
            }
        }
        if (m_inlineSheet)
        {
            chainVersion += m_inlineSheet->Version();
        }
        // Without a context there is no generation to key on (class edits on this view would
        // go unseen), so the cache is rebuilt every time - the pre-P1 cost, tests only.
        if (m_styleCache.valid && Context != nullptr && m_styleCache.generation == generation &&
            m_styleCache.chainVersion == chainVersion && m_styleCache.state == state)
        {
            return m_styleCache;
        }

        StyleCache& cache = m_styleCache;
        cache.rules.Clear();
        // Ascending cascade order: the context sheet, then the local sheets from the OUTERMOST
        // ancestor inward (nearer wins), then this view's inline sheet (wins over everything).
        if (Context != nullptr)
        {
            if (const StyleSheet* sheet = Context->GetStyleSheet())
            {
                sheet->CollectMatching(*this, state, StringView{}, cache.rules);
            }
        }
        for (usize i = depth; i-- > 0;)
        {
            chain[i]->m_localStyleSheet->CollectMatching(*this, state, StringView{}, cache.rules);
        }
        if (m_inlineSheet)
        {
            m_inlineSheet->CollectMatching(*this, state, StringView{}, cache.rules);
        }

        for (usize p = 0; p < static_cast<usize>(StyleProperty::COUNT); ++p)
        {
            cache.winners[p] = nullptr;
        }
        // Last declaration wins: walk from the highest-priority rule down, first fill wins.
        usize filled = 0;
        for (usize i = cache.rules.Size(); i-- > 0 && filled < static_cast<usize>(StyleProperty::COUNT);)
        {
            const StyleRule* rule = cache.rules[i];
            for (usize e = 0; e < rule->PropertyCount(); ++e)
            {
                const usize p = static_cast<usize>(rule->GetProperty(e).Prop);
                if (p < static_cast<usize>(StyleProperty::COUNT) && cache.winners[p] == nullptr)
                {
                    cache.winners[p] = rule;
                    ++filled;
                }
            }
        }
        cache.valid = true;
        cache.generation = generation;
        cache.chainVersion = chainVersion;
        cache.state = state;
        return cache;
    }

    StyleValue View::RawStyleValue(StyleProperty prop)
    {
        const StyleCache& cache = EnsureStyleCache();
        const usize p = static_cast<usize>(prop);
        if (p >= static_cast<usize>(StyleProperty::COUNT) || cache.winners[p] == nullptr)
        {
            return StyleValue::None();
        }
        if (Optional<StyleValue> v = cache.winners[p]->GetValue(prop); v.HasValue())
        {
            return v.Value();
        }
        return StyleValue::None();
    }

    StyleValue View::ResolveVariableValue(const StyleValue& reference, i32 depth)
    {
        const VariableReference* ref = reference.Variable();
        if (ref == nullptr || depth > 8)
        {
            return StyleValue::None();
        }
        StyleValue value = CustomProperty(ref->Name.AsView());
        if (value.IsNone())
        {
            value = VariableFallback(*ref);
        }
        if (value.GetKind() == StyleValue::Kind::Variable)
        {
            return ResolveVariableValue(value, depth + 1);
        }
        return value;
    }

    StyleValue View::ResolveKeywords(StyleProperty prop, const StyleValue& raw, i32 depth)
    {
        switch (raw.GetKind())
        {
        case StyleValue::Kind::Inherit:
            return Parent != nullptr ? Parent->ResolveStyle(prop) : StyleValue::None();
        case StyleValue::Kind::Initial:
            return StyleValue::None();
        case StyleValue::Kind::Variable:
        {
            StyleValue value = ResolveVariableValue(raw, depth);
            if (value.NeedsResolution() && depth <= 8)
            {
                return ResolveKeywords(prop, value, depth + 1);
            }
            return value;
        }
        default:
            return raw;
        }
    }

    StyleValue View::ResolveStyle(StyleProperty prop)
    {
        const StyleValue raw = RawStyleValue(prop);
        if (!raw.IsNone())
        {
            const StyleValue value = ResolveKeywords(prop, raw, 0);
            if (!value.IsNone() || raw.GetKind() == StyleValue::Kind::Initial)
            {
                return value;
            }
            // An unset variable with no fallback behaves like "unset": fall through to inherit.
        }
        if (IsInheritableStyle(prop) && Parent != nullptr)
        {
            return Parent->ResolveStyle(prop);
        }
        return StyleValue::None();
    }

    StyleValue View::CustomProperty(StringView name)
    {
        const u64 hash = HashText(name);
        for (View* v = this; v != nullptr; v = v->Parent)
        {
            const StyleCache& cache = v->EnsureStyleCache();
            for (usize i = cache.rules.Size(); i-- > 0;)
            {
                if (const StyleValue* value = cache.rules[i]->FindCustom(hash, name))
                {
                    if (value->GetKind() == StyleValue::Kind::Variable)
                    {
                        return v->ResolveVariableValue(*value, 1);
                    }
                    return *value;
                }
            }
        }
        return StyleValue::None();
    }

    f32 View::ResolveStyleLength(StyleProperty prop, f32 referenceSize, f32 defaultVal)
    {
        const StyleValue value = ResolveStyle(prop);
        if (Optional<f32> f = value.AsFloat(); f.HasValue())
        {
            return f.Value();
        }
        if (Optional<Unit> length = value.AsLength(); length.HasValue())
        {
            // em: of this view's computed font size - which, for font-size ITSELF, is the
            // parent's (CSS), so `font-size: 1.5em` compounds down the tree.
            const f32 fontSize =
                prop == StyleProperty::FontSize
                    ? (Parent != nullptr ? Parent->ResolveStyleLength(StyleProperty::FontSize, 0.0f, 16.0f) : 16.0f)
                    : ResolveStyleLength(StyleProperty::FontSize, 0.0f, 16.0f);
            RootView* root = Root();
            const f32 dpiScale = (root != nullptr) ? Max(root->DpiScale, 0.01f) : 1.0f;
            return length.Value().Resolve(dpiScale, referenceSize, fontSize);
        }
        return defaultVal;
    }

    StyleValue View::ResolvePartStyle(StringView part, StyleProperty prop, ControlState partState)
    {
        // Parts are uncached (per-part state varies per draw); same cascade order as the element.
        Array<const StyleRule*> rules;
        if (Context != nullptr)
        {
            if (const StyleSheet* sheet = Context->GetStyleSheet())
            {
                sheet->CollectMatching(*this, partState, part, rules);
            }
        }
        View* chain[64];
        usize depth = 0;
        for (View* anc = this; anc != nullptr && depth < 64; anc = anc->Parent)
        {
            if (anc->m_localStyleSheet)
            {
                chain[depth++] = anc;
            }
        }
        for (usize i = depth; i-- > 0;)
        {
            chain[i]->m_localStyleSheet->CollectMatching(*this, partState, part, rules);
        }
        if (m_inlineSheet)
        {
            m_inlineSheet->CollectMatching(*this, partState, part, rules);
        }
        for (usize i = rules.Size(); i-- > 0;)
        {
            if (Optional<StyleValue> v = rules[i]->GetValue(prop); v.HasValue())
            {
                return ResolveKeywords(prop, v.Value(), 0);
            }
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
    void View::RefreshEffectiveLayout()
    {
        // The effective copy marks sheet-filled fields DECLARED too: consumers ask "is Right
        // set?" of the effective layout and must not care where the value came from. The
        // inline m_layout keeps its own flags.
        LayoutStyle e = m_layout;
        m_styledOverflowHidden = false;
        const StyleCache& cache = EnsureStyleCache();
        // One cache lookup, then only the properties a rule actually sets pay for keyword /
        // variable resolution (RefreshEffectiveLayout runs at the top of every Measure).
        const auto set = [&cache](StyleProperty prop) {
            return cache.winners[static_cast<usize>(prop)] != nullptr;
        };
        const auto sizeSpec = [&](StyleProperty prop, Declared<SizeSpec>& field) {
            if (field.IsDeclared() || !set(prop))
            {
                return;
            }
            const StyleValue v = ResolveStyle(prop);
            if (Optional<StringView> word = v.AsString(); word.HasValue())
            {
                const StringView w = word.Value();
                if (w == StringView(u8"match") || w == StringView(u8"match-parent") ||
                    w == StringView(u8"fill") || w == StringView(u8"stretch"))
                {
                    field = SizeSpec::Match();
                }
                else if (w == StringView(u8"wrap") || w == StringView(u8"wrap-content") ||
                         w == StringView(u8"auto"))
                {
                    field = SizeSpec::Wrap();
                }
            }
            else if (Optional<f32> f = v.AsFloat(); f.HasValue())
            {
                field = SizeSpec::Fixed(Unit::Dp(f.Value()));
            }
            else if (Optional<Unit> u = v.AsLength(); u.HasValue())
            {
                field = SizeSpec::Fixed(u.Value());
            }
        };
        const auto unit = [&](StyleProperty prop, Declared<Unit>& field) {
            if (field.IsDeclared() || !set(prop))
            {
                return;
            }
            const StyleValue v = ResolveStyle(prop);
            if (Optional<f32> f = v.AsFloat(); f.HasValue())
            {
                field = Unit::Dp(f.Value());
            }
            else if (Optional<Unit> u = v.AsLength(); u.HasValue())
            {
                field = u.Value();
            }
        };
        const auto number = [&](StyleProperty prop, Declared<f32>& field) {
            if (field.IsDeclared() || !set(prop))
            {
                return;
            }
            // Insets/flex factors: absolute lengths (dp/px/em) resolve here; a percent inset
            // has no reference box at refresh time and contributes 0 (deviation, see spec).
            field = ResolveStyleLength(prop, 0.0f, field.value);
        };
        sizeSpec(StyleProperty::Width, e.Width);
        sizeSpec(StyleProperty::Height, e.Height);
        if (!e.Margin.IsDeclared() && set(StyleProperty::Margin))
        {
            if (Optional<Thickness> t = ResolveStyle(StyleProperty::Margin).AsThickness(); t.HasValue())
            {
                e.Margin = t.Value();
            }
        }
        unit(StyleProperty::MinWidth, e.MinWidth);
        unit(StyleProperty::MinHeight, e.MinHeight);
        unit(StyleProperty::MaxWidth, e.MaxWidth);
        unit(StyleProperty::MaxHeight, e.MaxHeight);
        number(StyleProperty::Top, e.Top);
        number(StyleProperty::Right, e.Right);
        number(StyleProperty::Bottom, e.Bottom);
        number(StyleProperty::Left, e.Left);
        number(StyleProperty::FlexGrow, e.FlexGrow);
        number(StyleProperty::FlexShrink, e.FlexShrink);
        if (!e.ZIndex.IsDeclared() && set(StyleProperty::ZIndex))
        {
            e.ZIndex = static_cast<i32>(ResolveStyleFloat(StyleProperty::ZIndex, 0.0f));
        }
        if (!e.Position.IsDeclared() && set(StyleProperty::Position))
        {
            const String word = ResolveStyleString(StyleProperty::Position);
            e.Position = word == StringView(u8"absolute") ? Position::Absolute : Position::Static;
        }
        if (!e.AlignSelf.HasValue() && set(StyleProperty::AlignSelf))
        {
            const String word = ResolveStyleString(StyleProperty::AlignSelf);
            const StringView w = word.AsView();
            if (w == StringView(u8"start") || w == StringView(u8"flex-start"))
                e.AlignSelf = Align::Start;
            else if (w == StringView(u8"end") || w == StringView(u8"flex-end"))
                e.AlignSelf = Align::End;
            else if (w == StringView(u8"center"))
                e.AlignSelf = Align::Center;
            else if (w == StringView(u8"stretch"))
                e.AlignSelf = Align::Stretch;
            else if (w == StringView(u8"baseline"))
                e.AlignSelf = Align::Baseline;
        }
        if (set(StyleProperty::Overflow))
        {
            const String word = ResolveStyleString(StyleProperty::Overflow);
            m_styledOverflowHidden = word == StringView(u8"hidden");
        }
        m_effectiveLayout = e;
    }

    void View::Measure(BoxConstraints c)
    {
        // Effective layout first: this view's (its own spec/margin below) and its children's
        // (the container's OnMeasure reads child->Layout() before measuring each child).
        RefreshEffectiveLayout();
        RefreshChildEffectiveLayouts();

        const BoxMetrics metrics = ResolveBoxMetrics();
        BoxConstraints box = c.Deflate(metrics.Margin);

        const LayoutStyle& layout = m_effectiveLayout;
        const SizeSpec widthSpec = layout.Width;
        const SizeSpec heightSpec = layout.Height;
        const bool clampsW = layout.MinWidth.Value() != Unit{} || layout.MaxWidth.Value() != Unit{};
        const bool clampsH = layout.MinHeight.Value() != Unit{} || layout.MaxHeight.Value() != Unit{};
        if (widthSpec.kind == SizeSpec::Kind::Fixed || heightSpec.kind == SizeSpec::Kind::Fixed ||
            clampsW || clampsH)
        {
            RootView* root = Root();
            const f32 dpiScale = (root != nullptr) ? Max(root->DpiScale, 0.01f) : 1.0f;
            // Percent resolves against the CONTAINING box on the same axis (the incoming
            // constraint's max, after margin; 0 when unbounded), em against the computed font
            // size. Both are only computed when a component needs them.
            const bool relative = widthSpec.fixedSize.IsRelative() || heightSpec.fixedSize.IsRelative() ||
                                  layout.MinWidth->IsRelative() || layout.MaxWidth->IsRelative() ||
                                  layout.MinHeight->IsRelative() || layout.MaxHeight->IsRelative();
            const f32 fontSize = relative ? ResolveStyleLength(StyleProperty::FontSize, 0.0f, 16.0f) : 0.0f;
            const f32 referenceW = BoxConstraints::IsBounded(box.MaxWidth) ? box.MaxWidth : 0.0f;
            const f32 referenceH = BoxConstraints::IsBounded(box.MaxHeight) ? box.MaxHeight : 0.0f;
            if (widthSpec.kind == SizeSpec::Kind::Fixed)
            {
                const f32 w = Max(0.0f, widthSpec.ResolveFixed(dpiScale, referenceW, fontSize));
                box.MinWidth = w;
                box.MaxWidth = w;
            }
            if (heightSpec.kind == SizeSpec::Kind::Fixed)
            {
                const f32 h = Max(0.0f, heightSpec.ResolveFixed(dpiScale, referenceH, fontSize));
                box.MinHeight = h;
                box.MaxHeight = h;
            }
            // min/max clamp the constraint band (a Fixed size included); max wins over min
            // when they cross, like CSS.
            if (clampsW)
            {
                if (layout.MinWidth.Value() != Unit{})
                {
                    const f32 minW = Max(0.0f, layout.MinWidth->Resolve(dpiScale, referenceW, fontSize));
                    box.MinWidth = Max(box.MinWidth, minW);
                    box.MaxWidth = Max(box.MaxWidth, minW);
                }
                if (layout.MaxWidth.Value() != Unit{})
                {
                    const f32 maxW = Max(0.0f, layout.MaxWidth->Resolve(dpiScale, referenceW, fontSize));
                    box.MaxWidth = Min(box.MaxWidth, maxW);
                    box.MinWidth = Min(box.MinWidth, maxW);
                }
            }
            if (clampsH)
            {
                if (layout.MinHeight.Value() != Unit{})
                {
                    const f32 minH = Max(0.0f, layout.MinHeight->Resolve(dpiScale, referenceH, fontSize));
                    box.MinHeight = Max(box.MinHeight, minH);
                    box.MaxHeight = Max(box.MaxHeight, minH);
                }
                if (layout.MaxHeight.Value() != Unit{})
                {
                    const f32 maxH = Max(0.0f, layout.MaxHeight->Resolve(dpiScale, referenceH, fontSize));
                    box.MaxHeight = Min(box.MaxHeight, maxH);
                    box.MinHeight = Min(box.MinHeight, maxH);
                }
            }
        }

        const Thickness chrome = metrics.Chrome();
        const Float2 content = OnMeasureContent(box.Deflate(chrome));
        if (content.x >= 0.0f)
        {
            MeasuredSize = Float2{box.ConstrainWidth(content.x + chrome.TotalHorizontal()),
                                  box.ConstrainHeight(content.y + chrome.TotalVertical())};
        }
        else
        {
            OnMeasure(box); // legacy seam - the control handles its own chrome
        }
        // Absolute children never feed MeasuredSize; they are measured against the content
        // box this view just settled on.
        MeasureAbsoluteChildren(Max(0.0f, MeasuredSize.x - chrome.TotalHorizontal()),
                                Max(0.0f, MeasuredSize.y - chrome.TotalVertical()));
    }

    // The base arrange (margin inset + device-grid rounding). Rounds
    // EDGES independently (x and x+w each snap, width = snapped difference) so adjacent
    // rounded boxes stay gapless; local-grid rounding composes to the global grid because
    // every ancestor rounds too (integer sums stay integers, at fractional scales multiples
    // of 1/dpi stay multiples).
    void View::Layout(f32 x, f32 y, f32 width, f32 height)
    {
        const Thickness margin = m_effectiveLayout.Margin;
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
        LayoutAbsoluteChildren();
    }

    // Lazily create the RootView's PopupLayer (kept as the last child). Defined here because the
    // :popup_layer type is incomplete in the :view partition (module cycle) but complete in this impl unit.
    PopupLayer* RootView::GetPopupLayer()
    {
        if (!m_popupLayer)
        {
            RefPtr<PopupLayer> pl = MakeRef<PopupLayer>(MemoryAllocator());
            m_popupLayer = RefPtr<ViewGroup>(pl.Get()); // upcast + ref
            ViewGroup::AddView(pl.Get()); // base add (bypasses RootView's keep-last override)
        }
        return Cast<PopupLayer>(m_popupLayer.Get());
    }

    ViewGroup* ViewGroup::AddView(View* child)
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

    void ViewGroup::InsertView(View* child, usize index)
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
        if (Context != nullptr)
        {
            Context->InvalidateStyles(); // :first-child / :last-child follow the order
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

    namespace
    {
        [[nodiscard]] bool CompoundMatches(const SelectorCompound& c, const View& view,
                                           ControlState state)
        {
            if (c.UnknownType)
            {
                return false;
            }
            if (c.ViewType != nullptr && !IsDerivedFrom(view.GetType(), c.ViewType))
            {
                return false;
            }
            for (const String& cls : c.StyleClasses)
            {
                if (!view.HasClass(cls.AsView()))
                {
                    return false;
                }
            }
            if (c.Id.HasValue() && view.Name != c.Id.Value())
            {
                return false;
            }
            if (c.State.HasValue())
            {
                const ControlState required = c.State.Value();
                if (required != ControlState::Normal && !HasFlag(state, required))
                {
                    return false;
                }
            }
            if (c.Structural != StructuralMatch::None)
            {
                const ViewGroup* parent = Cast<ViewGroup>(view.Parent);
                if (HasStructural(c.Structural, StructuralMatch::FirstChild) &&
                    (parent == nullptr || parent->ChildCount() == 0 || parent->GetChildAt(0) != &view))
                {
                    return false;
                }
                if (HasStructural(c.Structural, StructuralMatch::LastChild) &&
                    (parent == nullptr || parent->ChildCount() == 0 ||
                     parent->GetChildAt(parent->ChildCount() - 1) != &view))
                {
                    return false;
                }
                if (HasStructural(c.Structural, StructuralMatch::Empty))
                {
                    const ViewGroup* self = Cast<ViewGroup>(&view);
                    if (self != nullptr && self->ChildCount() != 0)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        // Right-to-left with backtracking: a descendant step may match ANY ancestor, and the
        // steps beyond it must still match from there.
        [[nodiscard]] bool AncestorsMatch(const Array<SelectorAncestor>& steps, usize index,
                                          const View& from)
        {
            if (index >= steps.Size())
            {
                return true;
            }
            const SelectorAncestor& step = steps[index];
            if (step.DirectParent)
            {
                const View* parent = from.Parent;
                return parent != nullptr &&
                       CompoundMatches(step.Compound, *parent, parent->GetControlState()) &&
                       AncestorsMatch(steps, index + 1, *parent);
            }
            for (const View* anc = from.Parent; anc != nullptr; anc = anc->Parent)
            {
                if (CompoundMatches(step.Compound, *anc, anc->GetControlState()) &&
                    AncestorsMatch(steps, index + 1, *anc))
                {
                    return true;
                }
            }
            return false;
        }
    }

    bool StyleSelector::Matches(const View& view, ControlState state,
                                StringView pseudoElement) const
    {
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
        if (UnknownType)
        {
            return false;
        }
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
        if (Id.HasValue() && view.Name != Id.Value())
        {
            return false;
        }
        if (State.HasValue())
        {
            const ControlState required = State.Value();
            if (required != ControlState::Normal && !HasFlag(state, required))
            {
                return false;
            }
        }
        if (Structural != StructuralMatch::None)
        {
            SelectorCompound structuralOnly;
            structuralOnly.Structural = Structural;
            if (!CompoundMatches(structuralOnly, view, state))
            {
                return false;
            }
        }
        return Ancestors.IsEmpty() || AncestorsMatch(Ancestors, 0, view);
    }

    void StyleSheet::CollectMatching(const View& view, ControlState state, StringView pseudo,
                                     Array<const StyleRule*>& out) const
    {
        // Gather in source order, then stable-sort by specificity: equal specificity keeps
        // declaration order, so the later rule ends up later (and wins).
        const usize first = out.Size();
        for (const RefPtr<StyleRule>& rule : m_rules)
        {
            if (rule->Selector.Matches(view, state, pseudo))
            {
                out.PushBack(rule.Get());
            }
        }
        for (usize i = first + 1; i < out.Size(); ++i)
        {
            const StyleRule* key = out[i];
            const i32 keySpecificity = key->Selector.Specificity();
            usize j = i;
            while (j > first && out[j - 1]->Selector.Specificity() > keySpecificity)
            {
                out[j] = out[j - 1];
                --j;
            }
            out[j] = key;
        }
    }

    StyleValue StyleSheet::Resolve(const View& view, StyleProperty prop) const
    {
        return ResolveMatching(view, view.GetControlState(), StringView{}, prop);
    }
}
