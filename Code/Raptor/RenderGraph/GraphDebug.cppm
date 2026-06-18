// Raptor::RenderGraph — :debug partition
//
// Debug visualization/reporting: Graphviz DOT export and a text summary. Ported
// from Sedulous.RenderGraph (GraphDebug.bf).

module;
#include "Core/Prelude.h"

export module raptor.rendergraph:debug;

import raptor.core;
import :types;
import :pass;
import :resource;
import :graph;

using namespace raptor::core;

export namespace raptor::rendergraph
{
    namespace detail
    {
        [[nodiscard]] inline StringView PassColor(RGPassType type)
        {
            switch (type)
            {
                case RGPassType::Render:  return u"#4488cc";
                case RGPassType::Compute: return u"#cc8844";
                case RGPassType::Copy:    return u"#44aa44";
            }
            return u"#888888";
        }
        [[nodiscard]] inline StringView LifetimeLabel(RGResourceLifetime lifetime)
        {
            switch (lifetime)
            {
                case RGResourceLifetime::Transient:  return u"transient";
                case RGResourceLifetime::Persistent: return u"persistent";
                case RGResourceLifetime::Imported:   return u"imported";
            }
            return u"?";
        }
        [[nodiscard]] inline StringView AccessLabel(RGAccessType type)
        {
            switch (type)
            {
                case RGAccessType::ReadTexture:          return u"read";
                case RGAccessType::ReadBuffer:           return u"read";
                case RGAccessType::ReadDepthStencil:     return u"depth-read";
                case RGAccessType::ReadCopySrc:          return u"copy-src";
                case RGAccessType::WriteColorTarget:     return u"color-out";
                case RGAccessType::WriteDepthTarget:     return u"depth-out";
                case RGAccessType::WriteStorage:         return u"storage-write";
                case RGAccessType::WriteCopyDst:         return u"copy-dst";
                case RGAccessType::ReadWriteStorage:     return u"rw-storage";
                case RGAccessType::ReadWriteDepthTarget: return u"depth-rw";
                case RGAccessType::ReadWriteColorTarget: return u"color-rw";
            }
            return u"?";
        }
        [[nodiscard]] inline StringView PassTypeLabel(RGPassType type)
        {
            switch (type)
            {
                case RGPassType::Render:  return u"Render";
                case RGPassType::Compute: return u"Compute";
                case RGPassType::Copy:    return u"Copy";
            }
            return u"?";
        }
    }

    class GraphDebug
    {
    public:
        // Graphviz DOT: pass nodes (boxes) + resource nodes (ellipse/diamond) +
        // access edges. Culled passes/edges are dashed/gray.
        static void ExportDOT(RenderGraph& graph, String& out)
        {
            const Array<RenderGraphPass*>& passes = graph.Passes();
            const Array<RenderGraphResource*>& resources = graph.Resources();

            out.Append(u"digraph RenderGraph {\n");
            out.Append(u"  rankdir=LR;\n");
            out.Append(u"  node [fontname=\"Helvetica\"];\n\n");

            for (usize i = 0; i < passes.Size(); ++i)
            {
                RenderGraphPass* pass = passes[i];
                const StringView style = pass->isCulled ? StringView(u"dashed") : StringView(u"filled");
                const StringView fontColor = pass->isCulled ? StringView(u"gray") : StringView(u"white");
                AppendFormat(out,
                    u"  pass{} [label=\"{}\" shape=box style={} fillcolor=\"{}\" fontcolor=\"{}\"",
                    i, pass->name.AsView(), style, detail::PassColor(pass->type), fontColor);
                if (pass->isCulled) { out.Append(u" color=gray"); }
                out.Append(u"];\n");
            }
            out.Append(u"\n");

            for (usize i = 0; i < resources.Size(); ++i)
            {
                RenderGraphResource* res = resources[i];
                if (res == nullptr) { continue; }
                const StringView shape = res->resourceType == RGResourceType::Texture
                                       ? StringView(u"ellipse") : StringView(u"diamond");
                AppendFormat(out, u"  res{} [label=\"{}\\n({})\" shape={}];\n",
                    i, res->name.AsView(), detail::LifetimeLabel(res->lifetime), shape);
            }
            out.Append(u"\n");

            for (usize passIdx = 0; passIdx < passes.Size(); ++passIdx)
            {
                RenderGraphPass* pass = passes[passIdx];
                for (const RGResourceAccess& access : pass->accesses)
                {
                    if (!access.handle.IsValid() || access.handle.index >= resources.Size()) { continue; }
                    if (resources[access.handle.index] == nullptr) { continue; }

                    const StringView label = detail::AccessLabel(access.type);
                    if (access.IsRead())
                    {
                        AppendFormat(out, u"  res{} -> pass{} [label=\"{}\"", access.handle.index, passIdx, label);
                        if (pass->isCulled) { out.Append(u" style=dashed color=gray"); }
                        out.Append(u"];\n");
                    }
                    if (access.IsWrite())
                    {
                        AppendFormat(out, u"  pass{} -> res{} [label=\"{}\"", passIdx, access.handle.index, label);
                        if (pass->isCulled) { out.Append(u" style=dashed color=gray"); }
                        out.Append(u"];\n");
                    }
                }
            }

            out.Append(u"}\n");
        }

        // Human-readable text summary (counts + execution order).
        static void ExportSummary(RenderGraph& graph, String& out)
        {
            const Array<RenderGraphPass*>& passes = graph.Passes();
            const Array<RenderGraphResource*>& resources = graph.Resources();
            const Array<i32>& executionOrder = graph.ExecutionOrder();

            usize activeCount = 0, culledCount = 0;
            for (RenderGraphPass* p : passes) { if (p->isCulled) { ++culledCount; } else { ++activeCount; } }

            usize resCount = 0, transientCount = 0, persistentCount = 0, importedCount = 0;
            for (RenderGraphResource* r : resources)
            {
                if (r == nullptr) { continue; }
                ++resCount;
                switch (r->lifetime)
                {
                    case RGResourceLifetime::Transient:  ++transientCount; break;
                    case RGResourceLifetime::Persistent: ++persistentCount; break;
                    case RGResourceLifetime::Imported:   ++importedCount; break;
                }
            }

            out.Append(u"=== Render Graph Summary ===\n");
            AppendFormat(out, u"Passes: {} active, {} culled, {} total\n", activeCount, culledCount, passes.Size());
            AppendFormat(out, u"Resources: {} total ({} transient, {} persistent, {} imported)\n",
                resCount, transientCount, persistentCount, importedCount);
            AppendFormat(out, u"Output: {}x{}\n\n", graph.OutputWidth(), graph.OutputHeight());

            if (!executionOrder.IsEmpty())
            {
                out.Append(u"Execution order:\n");
                for (usize i = 0; i < executionOrder.Size(); ++i)
                {
                    RenderGraphPass* pass = passes[static_cast<usize>(executionOrder[i])];
                    AppendFormat(out, u"  {}. [{}] {}\n", i + 1, detail::PassTypeLabel(pass->type), pass->name.AsView());
                }
            }
        }
    };
}
