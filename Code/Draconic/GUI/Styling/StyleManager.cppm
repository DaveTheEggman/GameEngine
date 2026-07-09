// Draconic GUI - :style_manager partition
//
// StyleManager: the glue that makes a StyleSheet live on a widget tree. It resolves each
// UIWidget against the sheet (+ MediaContext), applies the result, and - by caching the
// last-applied ResolvedStyle per widget - animates transitioned changes on the next apply.
// Re-applying after a state change (e.g. :hover) therefore drives CSS transitions live.
//
// The per-widget cache lives here (not on UIWidget) to avoid a :ui_widget <-> :style_sheet
// partition cycle. v1 re-resolves the whole subtree on ApplyTree; dirty-tracking (only
// re-resolve invalidated widgets) is a later optimization. Cache entries are keyed by raw
// Node* - call Forget()/Clear() when widgets are destroyed (lifecycle wiring deferred).

module;
#include "Core/Prelude.h"

export module draconic.gui:style_manager;

import draconic.core;    // HashMap, Cast, Move
import :node;
import :ui_widget;
import :style_sheet;
import :media_query;
import :style_applier;
import :transition;
import :resource_provider;

using namespace draconic::core;
namespace core = draconic::core;

export namespace draconic::gui
{
    class StyleManager
    {
    public:
        StyleManager() = default;
        explicit StyleManager(StyleSheet sheet) : m_sheet(core::Move(sheet)) {}

        void SetStyleSheet(StyleSheet sheet) { m_sheet = core::Move(sheet); m_cache = {}; } // hot-reload: drop cache
        [[nodiscard]] const StyleSheet& GetStyleSheet() const noexcept { return m_sheet; }

        void SetMediaContext(const MediaContext& context) { m_context = context; }
        [[nodiscard]] const MediaContext& GetMediaContext() const noexcept { return m_context; }

        // The resource provider that resolves background-image/font-family in the sheet
        // (typically supplied by the theme). Null = those properties are skipped.
        void SetResourceProvider(IResourceProvider* resources) noexcept { m_resources = resources; }
        [[nodiscard]] IResourceProvider* GetResourceProvider() const noexcept { return m_resources; }

        // Resolve + apply one widget; animate any transitioned change versus its last apply.
        void ApplyTo(UIWidget& widget)
        {
            ResolvedStyle resolved = m_sheet.Resolve(widget, m_context);
            if (const ResolvedStyle* previous = m_cache.Find(&widget))
                ApplyStyleAnimated(widget, *previous, resolved, m_resources);
            else
                ApplyStyle(widget, resolved, m_resources);
            m_cache.InsertOrAssign(&widget, core::Move(resolved));
        }

        // Apply to every UIWidget in the subtree.
        void ApplyTree(Node& root)
        {
            if (UIWidget* widget = core::Cast<UIWidget>(&root))
                ApplyTo(*widget);
            for (usize i = 0; i < root.ChildCount(); ++i)
                if (Node* child = root.GetChildAt(i))
                    ApplyTree(*child);
        }

        // Drop a widget's cached style (call before it is destroyed).
        void Forget(Node* widget) { m_cache.Remove(widget); }
        void Clear() { m_cache = {}; }

    private:
        StyleSheet m_sheet;
        MediaContext m_context;
        IResourceProvider* m_resources = nullptr; // non-owning; resolves url()/font-family
        HashMap<Node*, ResolvedStyle> m_cache; // last-applied style per widget (non-owning keys)
    };
}
