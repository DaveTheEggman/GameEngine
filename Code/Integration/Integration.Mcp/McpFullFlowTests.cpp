// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Integration.Mcp - the FULL agent-shaped sequence: one
// golden that walks the whole workflow THROUGH THE TOOLS, in the order the skill teaches:
// create -> open -> import a real script source -> cook -> author a scene -> validate ->
// health -> host state. Every step is a tools/call; nothing touches the project behind the
// server's back except the initial on-disk script file (the OS artifact an import consumes).
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "Core/Prelude.h"
import foundation.core;
import foundation.json;
import foundation.content;
import foundation.scene;
import foundation.scene.resource;
import foundation.mcp;
import pipeline.core;
import pipeline.importer;
import pipeline.registration;
import script.pipeline;
import engine.scenesurface;
import editor.core;
import editor.mcp;

using namespace foundation::core;
using namespace foundation::mcp;
namespace json = foundation::json;
using foundation::json::JsonValue;
namespace scene = foundation::scene;

namespace
{
    JsonValue FfCall(McpServer& s, StringView tool, JsonValue arguments)
    {
        JsonValue params = JsonValue::MakeObject();
        params.Set(u8"name", JsonValue::MakeString(String(tool)));
        params.Set(u8"arguments", Move(arguments));
        JsonValue req = JsonValue::MakeObject();
        req.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
        req.Set(u8"id", JsonValue::MakeNumber(1));
        req.Set(u8"method", JsonValue::MakeString(u8"tools/call"));
        req.Set(u8"params", Move(params));
        Optional<String> line = s.HandleLine(req.ToString().AsView());
        REQUIRE(line.HasValue());
        JsonValue resp = json::Parse(line.Value().AsView()).value;
        REQUIRE(resp.Has(u8"result"));
        REQUIRE(resp.Get(u8"result").Get(u8"isError").AsBool() == false);
        return JsonValue::Parse(
            resp.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString());
    }

    JsonValue FfStr(JsonValue o, StringView k, StringView v)
    {
        o.Set(String(k), JsonValue::MakeString(String(v)));
        return o;
    }
}

TEST_CASE("integration.mcp: the full agent flow - create, import, cook, author, validate, "
          "health, host state")
{
    std::error_code ec;
    std::filesystem::remove_all("mcp_full_project", ec);

    pipeline::BuilderRegistry builders{DefaultAllocator()};
    pipeline::ImporterRegistry importers{DefaultAllocator()};
    pipeline::RegisterPipelineTypes();
    pipeline::RegisterAllBuilders(builders);
    pipeline::RegisterAllImporters(importers);
    engine::RegisterAllSceneComponentReflection();

    McpServer server;
    editor::mcp::ProjectSession session;
    editor::mcp::RegisterProjectTools(server, session);
    editor::mcp::RegisterAssetTools(server, session);
    editor::mcp::RegisterAssetWriteTools(server, session, builders, importers);
    editor::mcp::RegisterSceneTools(server, session);
    editor::mcp::RegisterProjectHealthTool(server, session, builders);

    // 1. Create + open.
    (void)FfCall(server, u8"project_create",
                 FfStr(FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_full_project"),
                       u8"name", u8"Full"));
    (void)FfCall(server, u8"project_open",
                 FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_full_project"));

    // 2. Import a REAL script source (the Luau starter - it compiles) from disk.
    {
        pipeline::IScriptLanguageCook* cook =
            pipeline::ScriptLanguageCookRegistry::Get().FindByLanguage(u8"luau");
        REQUIRE(cook != nullptr);
        const StringView starter = cook->NewAssetTemplate(pipeline::ScriptTier::Behavior);
        std::ofstream file("Mover.luau", std::ios::binary);
        file.write(reinterpret_cast<const char*>(starter.Data()),
                   static_cast<std::streamsize>(starter.Size()));
    }
    JsonValue imported =
        FfCall(server, u8"asset_import", FfStr(JsonValue::MakeObject(), u8"source", u8"Mover.luau"));
    CHECK(imported.Get(u8"type").AsString() == StringView(u8"ScriptClassAsset"));

    // 3. Cook - the imported script compiles into the cooked database.
    JsonValue cooked = FfCall(server, u8"asset_cook", JsonValue::MakeObject());
    CHECK(cooked.Get(u8"cooked").AsNumber() >= 1.0);
    CHECK(cooked.Get(u8"failed").AsNumber() == doctest::Approx(0.0));

    // 4. Author a scene through the tools (seed text from a real SaveScene).
    String seedXml;
    {
        scene::Scene authored(DefaultAllocator(), u8"arena");
        engine::AddAllSceneManagers(authored);
        (void)authored.CreateEntity(u8"hero");
        auto* inst = session.project->SourceDb().RootGroup()->CreateInstance(
            u8"seed", scene::SceneDocument::StaticType());
        REQUIRE(scene::SaveScene(authored, *inst).IsOk());
        utf8char guid[37];
        inst->Id().ToChars(guid);
        JsonValue read = FfCall(server, u8"scene_read",
                                FfStr(JsonValue::MakeObject(), u8"guid", StringView(guid, 36)));
        seedXml = read.Get(u8"xml").AsString();
    }
    JsonValue written = FfCall(
        server, u8"scene_write",
        FfStr(FfStr(JsonValue::MakeObject(), u8"xml", seedXml.AsView()), u8"name", u8"level1"));
    const String sceneGuid(written.Get(u8"guid").AsString());

    // 5. Validate the stored scene.
    JsonValue valid = FfCall(server, u8"scene_validate",
                             FfStr(JsonValue::MakeObject(), u8"guid", sceneGuid.AsView()));
    CHECK(valid.Get(u8"valid").AsBool() == true);
    CHECK(valid.Get(u8"componentValidation").AsString() == StringView(u8"full"));

    // 6. Health: nothing broken. (The freshly written scenes stage rather than cook, so
    //    only the script counted as buildable - and it is cooked.)
    JsonValue health = FfCall(server, u8"project_health", JsonValue::MakeObject());
    CHECK(health.Get(u8"sound").AsBool() == true);
    CHECK(health.Get(u8"dirty").AsNumber() == doctest::Approx(0.0));
    CHECK(health.Get(u8"failedCooks").AsNumber() == doctest::Approx(0.0));

    std::remove("Mover.luau");
}
