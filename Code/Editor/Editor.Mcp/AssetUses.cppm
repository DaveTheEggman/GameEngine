// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Mcp - :asset_uses partition
//
// asset_uses: the REVERSE dependency query - "what uses
// this asset". Required reading before any destructive change (delete/rename/move): the agent
// sees every direct user and the kind of each edge before it breaks one.
//
// Edges come from the SAME sources the engine itself uses, computed LIVE (never a cached graph):
//   - buildable assets: the builder's ScanDependencies (exactly what the cook driver hashes) -
//     `reads` (content consumed at cook time) and `references` (the product's runtime refs);
//   - scenes/prefabs: LoadScene over the FULL manager set (Engine.SceneSurface) + a factory-less
//     ResourceManager whose unresolved set IS the component Ref list (the export
//     reachability-scanner recipe), plus each parked prefab instance's id;
//   - project settings: the manifest's Guid fields (default scene, startup script, input map,
//     bus layout, UI theme, UI font, loading document).
// Direct users only - re-run on a user to walk the chain outward.

module;
#include "Core/Prelude.h"

export module editor.mcp:asset_uses;

import foundation.core;
import foundation.json;
import foundation.content;
import foundation.vfs;
import foundation.resource;
import foundation.scene;
import foundation.scene.resource;
import foundation.mcp;
import pipeline.core;
import engine.scenesurface;
import editor.core;
import :session;

using namespace foundation::core;
using foundation::json::JsonValue;
namespace content = foundation::content;
namespace scene = foundation::scene;
namespace vfs = foundation::vfs;

namespace editor::mcp::detail
{
    inline bool ContainsGuid(const Array<Guid>& list, const Guid& id)
    {
        for (const Guid& g : list)
        {
            if (g == id)
            {
                return true;
            }
        }
        return false;
    }

    // One direct user of the queried asset: the using instance + every edge kind it holds.
    struct AssetUse
    {
        content::Instance* user = nullptr;
        String group; // slash-joined source-DB group path ("" at root)
        Array<String> edges;
    };

    // A scene/prefab instance's DIRECT references: component resource Refs (via a factory-less
    // ResourceManager - every bound id lands unresolved) + parked prefab-instance ids. The scratch
    // carries the full manager set, so no component's Refs are invisible. Returns false when the
    // stored stream does not load (project_health counts those; asset_uses skips them).
    inline bool CollectSceneReferences(content::Instance& instance, content::ContentDatabase& db,
                                       Array<Guid>& resources, Array<Guid>& prefabs)
    {
        scene::Scene scratch{editor::EditorRootAllocator()};
        engine::AddAllSceneManagers(scratch);
        if (!scene::LoadScene(instance, scratch).IsOk())
        {
            return false;
        }
        foundation::resource::ResourceManager collector(editor::EditorRootAllocator(), db); // no factories -> all binds unresolved
        scene::ResolveSceneResources(scratch, collector);
        collector.CollectUnresolved(resources);
        scratch.ForEachPendingPrefabInstance([&prefabs](scene::Scene::PendingPrefabInstance& pending)
                                             { prefabs.PushBack(pending.prefabId); });
        return true;
    }

    // Walk every source-DB instance (depth-first) and collect those with an edge to `target`.
    inline void CollectUses(content::Group* group, const String& path, const Guid& target,
                            content::ContentDatabase& db, pipeline::BuilderRegistry& builders,
                            vfs::IFileSystem& sourcesMount, Array<AssetUse>& out)
    {
        if (group == nullptr)
        {
            return;
        }
        for (content::Instance* inst : group->Instances())
        {
            if (inst->Id() == target)
            {
                continue; // self
            }
            AssetUse use;
            const bool isScene = inst->TypeName() == StringView(u8"SceneDocument");
            const bool isPrefab = inst->TypeName() == StringView(u8"PrefabDocument");
            if (isScene || isPrefab)
            {
                Array<Guid> resources;
                Array<Guid> prefabs;
                CollectSceneReferences(*inst, db, resources, prefabs);
                if (ContainsGuid(resources, target))
                {
                    use.edges.PushBack(String(u8"scene-resource"));
                }
                if (ContainsGuid(prefabs, target))
                {
                    use.edges.PushBack(String(u8"prefab-instance"));
                }
            }
            else if (pipeline::IAssetBuilder* builder = builders.FindByTypeName(inst->TypeName()))
            {
                RefPtr<ISerializable> object = inst->ReadObject();
                pipeline::Asset* asset = Cast<pipeline::Asset>(object.Get());
                if (asset != nullptr)
                {
                    pipeline::AssetBuildContext ctx{editor::EditorRootAllocator()};
                    ctx.sources = &sourcesMount;
                    ctx.source = inst;
                    ctx.db = &db;
                    pipeline::AssetDependencies deps;
                    builder->ScanDependencies(*asset, ctx, deps);
                    if (ContainsGuid(deps.reads, target))
                    {
                        use.edges.PushBack(String(u8"reads"));
                    }
                    if (ContainsGuid(deps.references, target))
                    {
                        use.edges.PushBack(String(u8"references"));
                    }
                }
            }
            if (!use.edges.IsEmpty())
            {
                use.user = inst;
                use.group = path;
                out.PushBack(Move(use));
            }
        }
        for (content::Group* sub : group->Groups())
        {
            const String childPath =
                path.IsEmpty() ? String(sub->Name()) : Format(u8"{}/{}", path.AsView(), sub->Name());
            CollectUses(sub, childPath, target, db, builders, sourcesMount, out);
        }
    }
}

export namespace editor::mcp
{
    // Registers asset_uses against `server`. `builders` is the host's registry (populated once
    // from Pipeline::Registration) and must outlive the server.
    inline void RegisterAssetUsesTool(foundation::mcp::McpServer& server, ProjectSession& session,
                                      pipeline::BuilderRegistry& builders)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;
        pipeline::BuilderRegistry* bld = &builders;

        server.RegisterTool(
            u8"asset_uses",
            u8"REVERSE dependency query: every DIRECT user of an asset, with the edge kind - "
            u8"'reads' (an asset's cook consumes its content), 'references' (an asset's cooked "
            u8"product refers to it at runtime), 'scene-resource' (a scene/prefab component "
            u8"references it), 'prefab-instance' (a scene/prefab instantiates it), plus any "
            u8"project-settings fields pointing at it (default scene, startup script, ...). "
            u8"Computed live from the source database. Call this BEFORE deleting, renaming, or "
            u8"moving an asset; an empty result means nothing in the source database or project "
            u8"settings points at it. Direct users only - re-run on a user to walk the chain.",
            SchemaBuilder()
                .Str(u8"guid", u8"the asset guid (canonical 8-4-4-4-12 form)", true)
                .Build(),
            [s, bld](const JsonValue& args) -> ToolResult
            {
                if (!s->project)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }
                const String guidText = args.Get(u8"guid").AsString();
                Guid id;
                if (!Guid::TryParse(guidText.AsView(), id))
                {
                    return Err(Format(u8"invalid guid '{}'", guidText.AsView()));
                }
                content::ContentDatabase& db = s->project->SourceDb();
                content::Instance* target = db.GetInstance(id);
                if (target == nullptr)
                {
                    return Err(Format(u8"no asset with guid '{}' in the source database "
                                      u8"(asset_uses answers over source assets; use asset_list "
                                      u8"to find the right guid)",
                                      guidText.AsView()));
                }

                vfs::NativeFileSystem sourcesMount(s->project->SourcesRoot().AsView(), editor::EditorRootAllocator());
                Array<detail::AssetUse> uses;
                detail::CollectUses(db.RootGroup(), String(), id, db, *bld, sourcesMount, uses);

                JsonValue usedBy = JsonValue::MakeArray();
                for (const detail::AssetUse& use : uses)
                {
                    JsonValue e = JsonValue::MakeObject();
                    e.Set(u8"guid", detail::GuidToJson(use.user->Id()));
                    e.Set(u8"name", JsonValue::MakeString(String(use.user->Name())));
                    e.Set(u8"type", JsonValue::MakeString(String(use.user->TypeName())));
                    if (!use.group.IsEmpty())
                    {
                        e.Set(u8"group", JsonValue::MakeString(use.group));
                    }
                    JsonValue edges = JsonValue::MakeArray();
                    for (const String& kind : use.edges)
                    {
                        edges.Add(JsonValue::MakeString(kind));
                    }
                    e.Set(u8"edges", Move(edges));
                    usedBy.Add(Move(e));
                }

                // The manifest's own references (a scene can be "in use" by the project itself).
                const auto& settings = s->project->Settings();
                JsonValue settingsUses = JsonValue::MakeArray();
                const struct
                {
                    const Guid* field;
                    StringView name;
                } settingsRefs[] = {
                    {&settings.defaultSceneId, u8"defaultScene"},
                    {&settings.startupScriptId, u8"startupScript"},
                    {&settings.defaultInputMapId, u8"defaultInputMap"},
                    {&settings.defaultBusLayoutId, u8"defaultBusLayout"},
                    {&settings.defaultUiThemeId, u8"defaultUiTheme"},
                    {&settings.defaultUiFontId, u8"defaultUiFont"},
                    {&settings.loadingDocumentId, u8"loadingDocument"},
                };
                for (const auto& ref : settingsRefs)
                {
                    if (*ref.field == id)
                    {
                        settingsUses.Add(JsonValue::MakeString(String(ref.name)));
                    }
                }

                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"guid", detail::GuidToJson(target->Id()));
                out.Set(u8"name", JsonValue::MakeString(String(target->Name())));
                out.Set(u8"type", JsonValue::MakeString(String(target->TypeName())));
                out.Set(u8"useCount", JsonValue::MakeNumber(static_cast<f64>(uses.Size())));
                out.Set(u8"usedBy", Move(usedBy));
                out.Set(u8"projectSettingsUses", Move(settingsUses));
                return out;
            });
    }
}
