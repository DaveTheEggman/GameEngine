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
import :operations;
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
            scene::Scene scratch(editor::EditorRootAllocator(), u8"__mcp_export_transcode");
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
    // The INLINE export - what the stdio host runs on the calling thread and what the export
    // CLI does: templates from the shared root plus the host tool directory (the player next to
    // the executable), scene streams pre-transcoded over the full manager set, the reachability
    // scanner over the same scan asset_uses runs, then ExportOne with the cook folded in.
    // `hostToolDir` is the host executable's directory; `dataRoot` the engine data root (the
    // shader cook reads <dataRoot>/Shaders).
    [[nodiscard]] inline OperationStep<ExportOutcome>
    RunExportInline(ProjectSession& session, pipeline::BuilderRegistry& builders,
                    StringView hostToolDir, StringView dataRoot, const ExportRequest& request)
    {
        editor::TemplateRegistry templates;
        {
            const String templatesRoot = editor::ResolveTemplatesRoot();
            UniquePtr<vfs::NativeFileSystem> rootFs;
            if (DirectoryExists(templatesRoot.AsView()))
            {
                rootFs = MakeUnique<vfs::NativeFileSystem>(
                    editor::EditorRootAllocator(), templatesRoot.AsView(), editor::EditorRootAllocator());
            }
            vfs::NativeFileSystem toolFs(hostToolDir, editor::EditorRootAllocator());
            templates.Refresh(templatesRoot.AsView(), rootFs.Get(), hostToolDir, &toolFs);
        }
        HashMap<Guid, Array<byte>> sceneStreams;
        detail::CollectExportSceneStreams(session.project->SourceDb().RootGroup(), sceneStreams);
        const editor::SceneReferenceScanner scanner =
            [](content::Instance& instance, content::ContentDatabase& db,
               editor::SceneReferences& out)
        { (void)detail::CollectSceneReferences(instance, db, out.resources, out.prefabs); };
        ExportOutcome outcome;
        if (!editor::ExportOne(*session.project, request.preset, templates, builders,
                               request.outRoot.AsView(), dataRoot, request.rebuild,
                               &outcome.result, {}, /*cook=*/true, &sceneStreams, &scanner)
                 .IsOk())
        {
            return Err(Format(u8"export of preset '{}' failed - read log_read (category "
                              u8"Export/Cook) for the failing step",
                              request.preset.name.AsView()));
        }
        return Optional<ExportOutcome>(Move(outcome));
    }

    // Registers project_export: preset resolution and the result shape here, the work through
    // the host's operations (inline on the stdio host, the editor's export job otherwise).
    inline void RegisterProjectExportTool(foundation::mcp::McpServer& server,
                                          ProjectSession& session, IProjectOperations& operations)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolOutcome;
        ProjectSession* s = &session;
        IProjectOperations* ops = &operations;
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
                foundation::mcp::ToolAnnotations::Rebuilds(),
            [s, ops](const JsonValue& args) -> ToolOutcome
            {
                if (!s->project)
                {
                    return Err(String(u8"no project is open (call project_open first)"));
                }

                // Presets: the project's file, else the synthesized host preset.
                editor::ExportPresetSet presets;
                {
                    vfs::NativeFileSystem projectFs(s->project->Directory(), editor::EditorRootAllocator());
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

                ExportRequest request;
                request.preset = *preset;
                const String outArg = args.Get(u8"out").AsString();
                request.outRoot = !outArg.IsEmpty() ? outArg
                                                    : PathJoin(s->project->Directory(), u8"Dist");
                request.rebuild = args.Get(u8"rebuild").AsBool();
                OperationStep<ExportOutcome> step = ops->Export(request);
                if (!step.HasValue())
                {
                    return Err(Move(step.Error()));
                }
                if (!step.Value().HasValue())
                {
                    return ToolOutcome::NotFinished(); // the host's export is still running
                }
                const editor::ExportResult& result = step.Value().Value().result;
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
