// Shared test doubles for the View cluster (faithful port of Sedulous.UI.Tests/src/TestHelpers.bf).
// Declared here, defined once in TestHelpers.cpp (DRACONIC_OBJECT type-info must be single-definition).
// The including TU must `import draconic.ui;` before including this header.
#pragma once
#include "Core/Reflection/Reflect.h"

namespace draconic::ui::tests
{
    /// Minimal concrete View with a fixed desired size.
    class TestView : public draconic::ui::View
    {
        DRACONIC_OBJECT(TestView, draconic::ui::View)
    public:
        draconic::core::f32 DesiredWidth = 50.0f;
        draconic::core::f32 DesiredHeight = 30.0f;

        TestView() = default;
        TestView(draconic::core::f32 w, draconic::core::f32 h) : DesiredWidth(w), DesiredHeight(h) {}

    protected:
        void OnMeasure(draconic::ui::BoxConstraints constraints) override;
    };

    /// Minimal concrete ViewGroup that lays out each child to fill its bounds.
    class TestGroup : public draconic::ui::ViewGroup
    {
        DRACONIC_OBJECT(TestGroup, draconic::ui::ViewGroup)
    protected:
        void OnLayout(draconic::core::f32 left, draconic::core::f32 top,
                      draconic::core::f32 width, draconic::core::f32 height) override;
    };

    /// Simple IListAdapter test double (Sedulous.UI.Tests SimpleListAdapter): a mutable Count and
    /// 100x30 TestView items. Not an Object, so it's header-inline (no DRACONIC_OBJECT needed).
    class SimpleListAdapter : public draconic::ui::ListAdapterBase
    {
    public:
        draconic::core::i32 Count = 0;
        explicit SimpleListAdapter(draconic::core::i32 count) : Count(count) {}
        [[nodiscard]] draconic::core::i32 ItemCount() const override { return Count; }
        [[nodiscard]] draconic::core::RefPtr<draconic::ui::View> CreateView(draconic::core::i32) override
        {
            return draconic::core::MakeRef<TestView>(draconic::core::DefaultAllocator(), 100.0f, 30.0f);
        }
        void BindView(draconic::ui::View*, draconic::core::i32) override {}
    };

    /// Sets up a UIContext + RootView (both owned by the caller).
    inline void Init(draconic::ui::UIContext& ctx, draconic::ui::RootView* root,
                     draconic::core::f32 width = 800.0f, draconic::core::f32 height = 600.0f)
    {
        root->ViewportSize = draconic::core::Float2{ width, height };
        ctx.AddRootView(root);
    }

    inline void LayoutPass(draconic::ui::UIContext& ctx, draconic::ui::RootView* root)
    {
        ctx.BeginFrame(0.016f);
        ctx.UpdateRootView(root);
    }
}
