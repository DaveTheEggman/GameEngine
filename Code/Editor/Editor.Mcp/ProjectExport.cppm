// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Mcp - :project_export partition
//
// project_export: a thin wrapper over the ONE export entry
// point (editor::ExportOne) - the same call the editor's Export menu and the export CLI make,
// so an MCP export produces an identical dist. Presets come from the project's
// export_presets.xml (else the synthesized host preset); the template registry resolves from
// the shared templates root plus the host tool directory (the player next to the executable);
// scene streams pre-transcode over the FULL manager set (Engine.SceneSurface) and the
// reachability scanner reuses the same full-manager scan asset_uses runs.

module;
#include "Core/Prelude.h"

export module editor.mcp:project_export;

import foundation.core;
import foundation.json;
import foundation.content;
import foundation.vfs;
import foundation.scene;
import foundation.scene.resource;
import foundation.mcp;
import pipeline.core;
import engine.scenesurface;
import editor.core;
import :session;
import :asset_uses; // CollectSceneReferences (the scene-edge scan the scanner delegates to)

using namespace foundation::core;
using foundation::json::JsonValue;
namespace content = foundation::content;
namespace scene = foundation::scene;
namespace vfs = foundation::vfs;

namespace editor::mcp::detail
{
    // Pre-transcode every scene/prefab TEXT source to the binary wire over the FULL manager
    // set - the same pass the export CLI runs (a manager-less transcode would drop records).
    inline void CollectExportSceneStreams(content::Group* group,
                                          HashMap<Guid, Array<byte>>& out)
    {
        if (group == nullptr)
        {
            return;
        }
        for (content::Instance* instance : group->Instances())
        {
            const bool isScene = instance->TypeName() == StringView(u8"SceneDocument");
            const bool isPrefab = instance->TypeName() == StringView(u8"PrefabDocument");
            if (!isScene && !isPrefab)
            {
                continue;
            }
            UniquePtr<IStream> stream = instance->ReadData(u8"scene");
            if (stream.Get() == nullptr)
            {
                continue;
            }
            scene::Scene scratch(u8"__mcp_export_transcode");
            engine::AddAllSceneManagers(scratch);
            Result<Array<byte>> bytes =
                scene::TranscodeSceneStreamToBinary(*stream, scratch, /*includeSettings=*/isScene);
            if (bytes.HasValue())
            {
                out.InsertOrAssign(instance->Id(), Move(bytes.Value()));
            }
        }
        for (content::Group* child : group->Groups())
        {
            CollectExportSceneStreams(child, out);
        }
    }
}

export namespace editor::mcp
{
    // Registers project_export. `builders` is the host's registry; `hostToolDir` is the
    // directory of the host executable (where Engine.Player + its runtime sidecars live -
    // the host template source, exactly as the export CLI resolves it).
    inline void RegisterProjectExportTool(foundation::mcp::McpServer& server,
                                          ProjectSession& session,
                                          pipeline::BuilderRegistry& builders,
                                          String hostToolDir)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;
        pipeline::BuilderRegistry* bld = &builders;

        server.RegisterTool(
            u8"project_export",
            u8"Export the open project into a shippable dist: cook everything, stage the "
            u8"scenes, pack the content, and stage the preset's player template - the same "
            u8"single entry point the editor's Export menu and the export CLI use, so the "
            u8"result is identical. Long-running (a full cook may run). Presets come from the "
            u8"project's export_presets.xml (default: the first; no file = a synthesized "
            u8"host-platform preset). Returns the output directory and the cook/stage/pack "
            u8"counts; run project_health first to catch breakage before a long export.",
            SchemaBuilder()
                .Str(u8"preset", u8"preset name (default: the project's first preset)")
                .Str(u8"out", u8"output root directory (default: <project>/Dist)")
                .Boolean(u8"rebuild", u8"force a full re-cook first (default incremental)")
                .Build(),
            [s, bld, hostToolDir](const JsonValue& args) -> ToolResult
            {
                if (!s->project)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }

                // Presets: the project's file, else the synthesized host preset.
                editor::ExportPresetSet presets;
                {
                    vfs::NativeFileSystem projectFs(s->project->Directory(), DefaultAllocator());
                    if (!editor::LoadExportPresets(projectFs, presets).IsOk())
                    {
                        editor::DefaultExportPresets(presets);
                    }
                }
                const String presetName = args.Get(u8"preset").AsString();
                const editor::ExportPreset* preset =
                    !presetName.IsEmpty()
                        ? presets.Find(presetName.AsView())
                        : (presets.presets.Size() > 0 ? &presets.presets[0] : nullptr);
                if (preset == nullptr)
                {
                    StringBuilder names;
                    for (usize i = 0; i < presets.presets.Size(); ++i)
                    {
                        if (i > 0)
                        {
                            names.Append(u8", ");
                        }
                        names.Append(presets.presets[i].name.AsView());
                    }
                    return Err(Format(u8"no preset named '{}' (available: {})",
                                      presetName.AsView(), names.Take().AsView()));
                }

                // Templates: the shared root + the host tool dir (player next to the exe).
                editor::TemplateRegistry templates;
                {
                    const String templatesRoot = editor::ResolveTemplatesRoot();
                    UniquePtr<vfs::NativeFileSystem> rootFs;
                    if (DirectoryExists(templatesRoot.AsView()))
                    {
                        rootFs = MakeUnique<vfs::NativeFileSystem>(
                            DefaultAllocator(), templatesRoot.AsView(), DefaultAllocator());
                    }
                    vfs::NativeFileSystem toolFs(hostToolDir.AsView(), DefaultAllocator());
                    templates.Refresh(templatesRoot.AsView(), rootFs.Get(), hostToolDir.AsView(),
                                      &toolFs);
                }

                // Scene streams (full-manager transcode) + the reachability scanner.
                HashMap<Guid, Array<byte>> sceneStreams;
                detail::CollectExportSceneStreams(s->project->SourceDb().RootGroup(),
                                                  sceneStreams);
                const editor::SceneReferenceScanner scanner =
                    [](content::Instance& instance, content::ContentDatabase& db,
                       editor::SceneReferences& out)
                { (void)detail::CollectSceneReferences(instance, db, out.resources, out.prefabs); };

                const String outArg = args.Get(u8"out").AsString();
                const String outRoot = !outArg.IsEmpty()
                                           ? outArg
                                           : PathJoin(s->project->Directory(), u8"Dist");
                const bool rebuild = args.Get(u8"rebuild").AsBool();

                editor::ExportResult result;
                if (!editor::ExportOne(*s->project, *preset, templates, *bld, outRoot.AsView(),
                                       rebuild, &result, {}, /*cook=*/true, &sceneStreams,
                                       &scanner)
                         .IsOk())
                {
                    return Err(Format(u8"export of preset '{}' failed - read log_read "
                                      u8"(category Export/Cook) for the failing step",
                                      preset->name.AsView()));
                }

                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"exported", JsonValue::MakeBool(true));
                out.Set(u8"preset", JsonValue::MakeString(preset->name));
                out.Set(u8"outputDir", JsonValue::MakeString(result.outputDir));
                out.Set(u8"cooked", JsonValue::MakeNumber(static_cast<f64>(result.content.cooked)));
                out.Set(u8"cookFailed",
                        JsonValue::MakeNumber(static_cast<f64>(result.content.cookFailed)));
                out.Set(u8"scenesStaged",
                        JsonValue::MakeNumber(static_cast<f64>(result.content.scenesStaged)));
                out.Set(u8"filesPacked",
                        JsonValue::MakeNumber(static_cast<f64>(result.content.filesPacked)));
                out.Set(u8"filesStaged",
                        JsonValue::MakeNumber(static_cast<f64>(result.filesStaged)));
                if (!result.engineVersionWarning.IsEmpty())
                {
                    out.Set(u8"warning", JsonValue::MakeString(result.engineVersionWarning));
                }
                if (result.pruning.pruned)
                {
                    JsonValue pruning = JsonValue::MakeObject();
                    pruning.Set(u8"kept",
                                JsonValue::MakeNumber(static_cast<f64>(result.pruning.keptCount)));
                    pruning.Set(u8"dropped", JsonValue::MakeNumber(
                                                 static_cast<f64>(result.pruning.dropped.Size())));
                    out.Set(u8"pruning", Move(pruning));
                }
                return out;
            });
    }
}
