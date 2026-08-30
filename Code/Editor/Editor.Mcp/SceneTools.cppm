// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Mcp - :scene_tools partition
//
// The scene/prefab MCP tools: scene_read / scene_write /
// scene_validate + prefab_read / prefab_write over the XML TEXT sources - files-are-truth, the
// editor is NOT involved. This is what makes an agent scene-capable headlessly: structural
// authoring through the same "scene" data stream the editor saves.
//
// Validation is FULL: the scratch Scene carries the COMPLETE engine manager set (the
// Engine.SceneSurface composition root - the same per-domain functions the subsystems inject
// through), so component payloads field-validate through their real managers, exactly as the
// editor and the cooked-scene reader would parse them. A record whose type resolves to no
// manager is now a GENUINELY unknown type; the reader skips it with a warning we capture and
// surface. Writes validate FIRST and refuse with the reasons; a passing write is
// byte-preserving (the agent's XML is stored verbatim - the reader accepted it, so the editor
// and cook will too).

module;
#include "Core/Prelude.h"

export module editor.mcp:scene_tools;

import foundation.core;
import foundation.json;
import foundation.content;
import foundation.scene;
import foundation.scene.resource;
import foundation.xml.serialization;
import foundation.mcp;
import editor.core;
import engine.scenesurface; // AddAllSceneManagers - the full manager set for the validate scratch
import :session;

using namespace foundation::core;
using foundation::json::JsonValue;
namespace content = foundation::content;
namespace scene = foundation::scene;

namespace editor::mcp::detail
{
    // Captures Scene-category log lines emitted while parsing (the reader WARNS and skips
    // unknown component types instead of failing - exactly what the agent needs to hear).
    class SceneLogCapture final : public ILogSink
    {
    public:
        Array<String> lines;
        void Write(LogLevel level, StringView category, StringView message) noexcept override
        {
            if (category == StringView(u8"Scene") && level >= LogLevel::Warning)
            {
                lines.PushBack(String(message));
            }
        }
    };

    struct SceneParseReport
    {
        bool valid = false;
        String error;           // empty when valid
        Array<String> warnings; // skipped/unknown component records etc.
        usize entityCount = 0;
        usize rootCount = 0;
        String sceneName;
    };

    // Parse `xml` as a scene stream into a scratch Scene carrying the FULL engine manager set
    // (Engine.SceneSurface), so component payloads validate through their real managers. Only a
    // genuinely unknown component type is skipped (warning, captured above).
    inline SceneParseReport ParseSceneXml(StringView xml)
    {
        SceneParseReport report;
        SceneLogCapture capture;
        GlobalLogger().AddSink(&capture);

        MemoryStream stream;
        (void)stream.Write(xml.Data(), xml.Size());
        (void)stream.Seek(0, SeekOrigin::Begin);

        scene::Scene scratch;
        engine::AddAllSceneManagers(scratch);
        Result<Array<byte>> transcoded =
            scene::TranscodeSceneStreamToBinary(stream, scratch, /*includeSettings=*/true);
        GlobalLogger().RemoveSink(&capture);

        report.warnings = Move(capture.lines);
        if (!transcoded.HasValue())
        {
            report.error = Format(u8"the scene stream did not parse (error {}) - fix the XML "
                                  u8"and validate again",
                                  static_cast<i32>(transcoded.Error()));
            return report;
        }
        report.valid = true;
        report.sceneName = String(scratch.Name());
        report.entityCount = scratch.EntityCount();
        for (scene::EntityHandle root = scratch.GetFirstRoot(); root.IsAssigned();
             root = scratch.GetNextSibling(root))
        {
            ++report.rootCount;
        }
        return report;
    }

    inline JsonValue ReportToJson(const SceneParseReport& report)
    {
        JsonValue out = JsonValue::MakeObject();
        out.Set(u8"valid", JsonValue::MakeBool(report.valid));
        if (!report.valid)
        {
            out.Set(u8"error", JsonValue::MakeString(report.error));
        }
        JsonValue warnings = JsonValue::MakeArray();
        for (const String& w : report.warnings)
        {
            warnings.Add(JsonValue::MakeString(w));
        }
        out.Set(u8"warnings", Move(warnings));
        if (report.valid)
        {
            out.Set(u8"sceneName", JsonValue::MakeString(report.sceneName));
            out.Set(u8"entityCount",
                    JsonValue::MakeNumber(static_cast<f64>(report.entityCount)));
            out.Set(u8"rootCount", JsonValue::MakeNumber(static_cast<f64>(report.rootCount)));
        }
        // Honesty marker: component payloads validate through the FULL engine manager set
        // (Engine.SceneSurface); warnings list any genuinely unknown component types.
        out.Set(u8"componentValidation", JsonValue::MakeString(u8"full"));
        return out;
    }

    // Resolve {guid} to an instance of the wanted document type; a wrong-type hit returns the
    // redirect message so the agent uses the sibling tool.
    inline content::Instance* ResolveDocument(editor::EditorProject* project, StringView guidText,
                                              StringView wantedType, StringView siblingTool,
                                              String& error)
    {
        if (project == nullptr)
        {
            error = String(u8"no project is open - call project_open first");
            return nullptr;
        }
        Guid id;
        if (!Guid::TryParse(guidText, id))
        {
            error = Format(u8"'{}' is not a valid guid", guidText);
            return nullptr;
        }
        content::Instance* instance = project->SourceDb().GetInstance(id);
        if (instance == nullptr)
        {
            error = Format(u8"no asset with guid {} in the open project", guidText);
            return nullptr;
        }
        if (instance->TypeName() != wantedType)
        {
            error = Format(u8"'{}' is a {}, not a {} - use {} instead", instance->Name(),
                           instance->TypeName(), wantedType, siblingTool);
            return nullptr;
        }
        return instance;
    }

    inline Result<String> ReadSceneStream(content::Instance& instance)
    {
        UniquePtr<IStream> stream = instance.ReadData(u8"scene");
        if (stream.Get() == nullptr)
        {
            return foundation::core::Err(ErrorCode::NotFound);
        }
        const i64 size = stream->Size();
        String text;
        if (size > 0)
        {
            Array<utf8char> buffer;
            buffer.Resize(static_cast<usize>(size));
            if (stream->Read(buffer.Data(), static_cast<u64>(size)) != static_cast<u64>(size))
            {
                return foundation::core::Err(ErrorCode::Internal);
            }
            text = String(StringView(buffer.Data(), buffer.Size()));
        }
        return text;
    }
}

export namespace editor::mcp
{
    /// scene_read / scene_write / scene_validate + prefab_read / prefab_write.
    inline void RegisterSceneTools(foundation::mcp::McpServer& server, ProjectSession& session)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;
        ProjectSession* s = &session;

        const auto makeRead = [s, &server](StringView tool, StringView wantedType,
                                           StringView sibling, StringView kind)
        {
            server.RegisterTool(
                String(tool),
                Format(u8"Read a {}'s XML source (the exact text the editor saves and the cook "
                       u8"consumes). Returns {{name, xml}}. Read this before editing; write "
                       u8"changes back with {}_write.",
                       kind, kind),
                SchemaBuilder().Str(u8"guid", Format(u8"the {} asset's guid", kind).AsView(), true)
                    .Build(),
                [s, wantedType = String(wantedType), sibling = String(sibling)](
                    const JsonValue& args) -> ToolResult
                {
                    String error;
                    content::Instance* instance = detail::ResolveDocument(
                        s->project.Get(), args.Get(u8"guid").AsString().AsView(),
                        wantedType.AsView(), sibling.AsView(), error);
                    if (instance == nullptr)
                    {
                        return Err(Move(error));
                    }
                    Result<String> xml = detail::ReadSceneStream(*instance);
                    if (!xml.HasValue())
                    {
                        return Err(Format(u8"'{}' has no scene stream (never saved?)",
                                          instance->Name()));
                    }
                    JsonValue out = JsonValue::MakeObject();
                    out.Set(u8"name", JsonValue::MakeString(String(instance->Name())));
                    out.Set(u8"xml", JsonValue::MakeString(xml.Value()));
                    return out;
                });
        };
        makeRead(u8"scene_read", u8"SceneDocument", u8"prefab_read", u8"scene");
        makeRead(u8"prefab_read", u8"PrefabDocument", u8"scene_read", u8"prefab");

        server.RegisterTool(
            u8"scene_validate",
            u8"Validate scene/prefab XML without writing anything - use this as the validation "
            u8"loop when authoring scenes. Pass `xml` (raw text) OR `guid` (validate the stored "
            u8"stream). Returns {valid, error?, warnings[], sceneName, entityCount, rootCount}. "
            u8"Validation is STRUCTURAL (entities/hierarchy/transforms/record framing); component "
            u8"payloads are checked in-engine at load, and unknown component types surface here "
            u8"as warnings.",
            SchemaBuilder()
                .Str(u8"xml", u8"scene XML text to validate", false)
                .Str(u8"guid", u8"a scene/prefab asset guid whose stored stream to validate",
                     false)
                .Build(),
            [s](const JsonValue& args) -> ToolResult
            {
                String xml = args.Get(u8"xml").AsString();
                const String guidText = args.Get(u8"guid").AsString();
                if (xml.IsEmpty() == guidText.IsEmpty())
                {
                    return Err(String(
                        u8"pass exactly one of `xml` (text to validate) or `guid` (stored "
                        u8"stream to validate)"));
                }
                if (!guidText.IsEmpty())
                {
                    if (s->project.Get() == nullptr)
                    {
                        return Err(String(u8"no project is open - call project_open first"));
                    }
                    Guid id;
                    if (!Guid::TryParse(guidText.AsView(), id))
                    {
                        return Err(Format(u8"'{}' is not a valid guid", guidText.AsView()));
                    }
                    content::Instance* instance = s->project->SourceDb().GetInstance(id);
                    if (instance == nullptr)
                    {
                        return Err(Format(u8"no asset with guid {} in the open project",
                                          guidText.AsView()));
                    }
                    Result<String> stored = detail::ReadSceneStream(*instance);
                    if (!stored.HasValue())
                    {
                        return Err(Format(u8"'{}' has no scene stream to validate",
                                          instance->Name()));
                    }
                    xml = stored.Value();
                }
                return detail::ReportToJson(detail::ParseSceneXml(xml.AsView()));
            });

        const auto makeWrite = [s, &server](StringView tool, StringView wantedType,
                                            StringView sibling, StringView kind,
                                            bool singleRoot)
        {
            server.RegisterTool(
                String(tool),
                Format(u8"Write a {}'s XML source. VALIDATES FIRST and refuses with the reasons "
                       u8"on failure (nothing is written then). Pass `guid` to overwrite an "
                       u8"existing {} OR `name` (+ optional `group` path) to create a new one. "
                       u8"On success the XML is stored verbatim as the asset's source of truth.",
                       kind, kind),
                SchemaBuilder()
                    .Str(u8"xml", Format(u8"the {} XML text to store", kind).AsView(), true)
                    .Str(u8"guid", Format(u8"an existing {} asset to overwrite", kind).AsView(),
                         false)
                    .Str(u8"name", u8"name for a NEW asset (when no guid)", false)
                    .Str(u8"group", u8"slash-joined group path for a NEW asset (default root)",
                         false)
                    .Build(),
                [s, wantedType = String(wantedType), sibling = String(sibling),
                 singleRoot](const JsonValue& args) -> ToolResult
                {
                    if (s->project.Get() == nullptr)
                    {
                        return Err(String(u8"no project is open - call project_open first"));
                    }
                    const String xml = args.Get(u8"xml").AsString();
                    if (xml.IsEmpty())
                    {
                        return Err(String(u8"`xml` is required and was empty"));
                    }
                    const detail::SceneParseReport report =
                        detail::ParseSceneXml(xml.AsView());
                    if (!report.valid)
                    {
                        JsonValue refusal = detail::ReportToJson(report);
                        return Err(Format(u8"refused - the XML did not validate: {}. Full "
                                          u8"report: {}",
                                          report.error.AsView(),
                                          refusal.ToString().AsView()));
                    }
                    if (singleRoot && report.rootCount != 1)
                    {
                        return Err(Format(
                            u8"refused - a prefab must have exactly ONE root entity (found "
                            u8"{}); re-parent the extra roots under one root first",
                            report.rootCount));
                    }

                    const String guidText = args.Get(u8"guid").AsString();
                    content::Instance* instance = nullptr;
                    if (!guidText.IsEmpty())
                    {
                        String error;
                        instance = detail::ResolveDocument(s->project.Get(), guidText.AsView(),
                                                           wantedType.AsView(), sibling.AsView(),
                                                           error);
                        if (instance == nullptr)
                        {
                            return Err(Move(error));
                        }
                    }
                    else
                    {
                        const String name = args.Get(u8"name").AsString();
                        if (name.IsEmpty())
                        {
                            return Err(String(
                                u8"pass `guid` (overwrite existing) or `name` (create new)"));
                        }
                        content::Group* group = detail::ResolveGroupPath(
                            s->project->SourceDb().RootGroup(),
                            args.Get(u8"group").AsString().AsView());
                        const TypeInfo* docType =
                            wantedType.AsView() == StringView(u8"SceneDocument")
                                ? &scene::SceneDocument::StaticType()
                                : &scene::PrefabDocument::StaticType();
                        instance = group->CreateInstance(name.AsView(), *docType);
                        if (instance == nullptr)
                        {
                            return Err(Format(u8"could not create '{}' (name already taken in "
                                              u8"that group?)",
                                              name.AsView()));
                        }
                    }

                    // Primary document (name discovery) + the verbatim XML stream.
                    if (wantedType.AsView() == StringView(u8"SceneDocument"))
                    {
                        scene::SceneDocument doc;
                        doc.name = report.sceneName.IsEmpty() ? String(instance->Name())
                                                              : report.sceneName;
                        if (!instance->WriteObject(doc).IsOk())
                        {
                            return Err(String(u8"failed writing the scene document envelope"));
                        }
                    }
                    else
                    {
                        scene::PrefabDocument doc;
                        doc.name = report.sceneName.IsEmpty() ? String(instance->Name())
                                                              : report.sceneName;
                        if (!instance->WriteObject(doc).IsOk())
                        {
                            return Err(String(u8"failed writing the prefab document envelope"));
                        }
                    }
                    const Status wrote = instance->WriteData(
                        u8"scene", Span<const byte>{reinterpret_cast<const byte*>(xml.CStr()),
                                                    xml.Size()});
                    if (!wrote.IsOk())
                    {
                        return Err(String(u8"failed writing the scene stream to disk"));
                    }

                    JsonValue out = detail::ReportToJson(report);
                    out.Set(u8"guid", detail::GuidToJson(instance->Id()));
                    out.Set(u8"name", JsonValue::MakeString(String(instance->Name())));
                    out.Set(u8"written", JsonValue::MakeBool(true));
                    return out;
                });
        };
        makeWrite(u8"scene_write", u8"SceneDocument", u8"prefab_write", u8"scene",
                  /*singleRoot=*/false);
        makeWrite(u8"prefab_write", u8"PrefabDocument", u8"scene_write", u8"prefab",
                  /*singleRoot=*/true);
    }
}
