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

import draconic.core;    // HashMap, Cast, Move, Array, StringView
import draconic.fonts;   // IFontService
import :node;
import :ui_node;
import :ui_widget;
import :style_sheet;
import :media_query;
import :style_applier;
import :transition;
import :resource_provider;

using namespace draconic::core;
namespace core = draconic::core;
namespace fonts = draconic::fonts;

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

        // Loads background-image assets referenced by the sheet. Null = background-image skipped.
        void SetResourceProvider(IResourceProvider* resources) noexcept { m_resources = resources; }
        [[nodiscard]] IResourceProvider* GetResourceProvider() const noexcept { return m_resources; }

        // Resolves font-family/-size in the sheet (the app's font service - VFS-backed or not;
        // the GUI is agnostic). Null = font-family skipped.
        void SetFontService(fonts::IFontService* fontService) noexcept { m_fontService = fontService; }
        [[nodiscard]] fonts::IFontService* GetFontService() const noexcept { return m_fontService; }

        // Resolve + apply one widget; animate any transitioned change versus its last apply.
        void ApplyTo(UIWidget& widget)
        {
            ResolvedStyle resolved = m_sheet.Resolve(widget, m_context);
            if (const ResolvedStyle* previous = m_cache.Find(&widget))
                ApplyStyleAnimated(widget, *previous, resolved, m_resources, m_fontService);
            else
                ApplyStyle(widget, resolved, m_resources, m_fontService);
            m_cache.InsertOrAssign(&widget, core::Move(resolved));

            // Pseudo-element parts (tag::part): resolve + apply each part the widget declares.
            Array<core::StringView> parts;
            widget.CollectStyleParts(parts);
            for (const core::StringView part : parts)
            {
                const ResolvedStyle partStyle = m_sheet.Resolve(widget, m_context, true, part);
                ApplyPartStyle(widget, part, partStyle);
            }
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
        IResourceProvider* m_resources = nullptr;    // non-owning; loads background-image assets
        fonts::IFontService* m_fontService = nullptr; // non-owning; resolves font-family
        HashMap<Node*, ResolvedStyle> m_cache; // last-applied style per widget (non-owning keys)
    };
}
