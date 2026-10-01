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
import engine.composition;
import editor.project;
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
        LineOutcome line = s.HandleLine(req.ToString().AsView());
        REQUIRE(line.state == LineState::Answered);
        JsonValue resp = json::Parse(line.response.AsView()).value;
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

    // Every occurrence of `from` in `text` replaced with `to` (byte-wise; test-local helper).
    String ReplaceAll(StringView text, StringView from, StringView to)
    {
        StringBuilder out;
        usize i = 0;
        while (i < text.Size())
        {
            if (i + from.Size() <= text.Size() && text.SubStr(i, from.Size()) == from)
            {
                out.Append(to);
                i += from.Size();
                continue;
            }
            out.Append(text[i]);
            ++i;
        }
        return out.Take();
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
    // The flow runs over the SHARED engine surface (what every host serves) plus the stdio
    // host's project_create / project_open.
    editor::EditorLogBuffer logBuffer{DefaultAllocator()};
    editor::mcp::ProjectSession session;
    editor::mcp::ProjectOwner owner;
    editor::mcp::InlineProjectOperations operations(session, builders, String(), String());
    pipeline::AssetCreatorRegistry creators{DefaultAllocator()};
    (void)pipeline::RegisterAllCreators(creators);
    editor::mcp::RegisterEngineTools(server, session, builders, importers, creators, logBuffer,
                                     editor::mcp::EngineToolPaths{}, operations);
    editor::mcp::RegisterProjectOpenTools(server, session, owner);

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

TEST_CASE("integration.mcp: RegisterEngineTools registers exactly kEngineToolCount tools - the "
          "surface every host serves, and only that")
{
    pipeline::BuilderRegistry builders{DefaultAllocator()};
    pipeline::ImporterRegistry importers{DefaultAllocator()};
    editor::EditorLogBuffer logBuffer{DefaultAllocator()};
    editor::mcp::ProjectSession session;

    McpServer server;
    editor::mcp::InlineProjectOperations operations(session, builders, String(), String());
    pipeline::AssetCreatorRegistry creators{DefaultAllocator()};
    (void)pipeline::RegisterAllCreators(creators);
    editor::mcp::RegisterEngineTools(server, session, builders, importers, creators, logBuffer,
                                     editor::mcp::EngineToolPaths{}, operations);
    CHECK(server.ToolCount() == editor::mcp::kEngineToolCount);

    JsonValue req = JsonValue::MakeObject();
    req.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
    req.Set(u8"id", JsonValue::MakeNumber(1));
    req.Set(u8"method", JsonValue::MakeString(u8"tools/list"));
    LineOutcome line = server.HandleLine(req.ToString().AsView());
    REQUIRE(line.state == LineState::Answered);
    JsonValue tools = json::Parse(line.response.AsView()).value.Get(u8"result").Get(u8"tools");
    const auto has = [&tools](StringView name)
    {
        for (usize i = 0; i < static_cast<usize>(tools.Count()); ++i)
        {
            if (tools.At(i).Get(u8"name").AsString().AsView() == name)
            {
                return true;
            }
        }
        return false;
    };
    // Spot checks across the families the root gathers ...
    CHECK(has(u8"type_list"));
    CHECK(has(u8"script_api"));
    CHECK(has(u8"project_info"));
    CHECK(has(u8"asset_cook"));
    CHECK(has(u8"scene_write"));
    CHECK(has(u8"project_export"));
    CHECK(has(u8"known_issues"));
    CHECK(has(u8"component_schema"));
    // ... and what a HOST adds itself: never part of the shared surface.
    CHECK_FALSE(has(u8"project_open"));
    CHECK_FALSE(has(u8"project_create"));
    CHECK_FALSE(has(u8"host_info"));
}

TEST_CASE("integration.mcp: LocateShippingDocs walks up to the checkout layout, accepts the "
          "distribution layout, and leaves a miss empty")
{
    std::error_code ec;
    std::filesystem::remove_all("mcp_docs_checkout", ec);
    std::filesystem::remove_all("mcp_docs_dist", ec);
    std::filesystem::create_directories("mcp_docs_checkout/Documentation/Shipping", ec);
    std::filesystem::create_directories("mcp_docs_checkout/Bin/Debug", ec);
    std::filesystem::create_directories("mcp_docs_dist/tool", ec);
    std::ofstream("mcp_docs_checkout/Documentation/Shipping/KnownIssues.md") << "# known";
    std::ofstream("mcp_docs_checkout/Documentation/Shipping/McpGuide.md") << "# guide";
    std::ofstream("mcp_docs_dist/KnownIssues.md") << "# staged";

    // The engine checkout: the executable sits under Bin/, the docs two levels up.
    {
        editor::mcp::EngineToolPaths paths;
        const String starts[] = {String(u8"mcp_docs_checkout/Bin/Debug")};
        editor::mcp::LocateShippingDocs(Span<const String>(starts, 1), paths);
        CHECK(paths.shippingDocsDir == u8"mcp_docs_checkout/Documentation/Shipping");
        CHECK(paths.knownIssues == u8"mcp_docs_checkout/Documentation/Shipping/KnownIssues.md");
    }
    // The served docs list by NAME, whatever order the filesystem hands them back in (a third
    // file written last would otherwise come last on some filesystems and first on others).
    {
        std::ofstream("mcp_docs_checkout/Documentation/Shipping/Assets.md") << "# assets";
        McpServer server;
        editor::mcp::RegisterShippingDocResources(server,
                                                  u8"mcp_docs_checkout/Documentation/Shipping");
        LineOutcome line = server.HandleLine(
            u8"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/list\",\"params\":{}}");
        REQUIRE(line.state == LineState::Answered);
        const JsonValue resources =
            json::Parse(line.response.AsView()).value.Get(u8"result").Get(u8"resources");
        REQUIRE(resources.Count() == 3);
        CHECK(resources.At(0).Get(u8"uri").AsString() == StringView(u8"docs://Assets.md"));
        CHECK(resources.At(1).Get(u8"uri").AsString() == StringView(u8"docs://KnownIssues.md"));
        CHECK(resources.At(2).Get(u8"uri").AsString() == StringView(u8"docs://McpGuide.md"));
    }
    // A distribution: KnownIssues.md staged beside the tool, no docs directory at all.
    {
        editor::mcp::EngineToolPaths paths;
        const String starts[] = {String(u8"mcp_docs_dist/tool")};
        editor::mcp::LocateShippingDocs(Span<const String>(starts, 1), paths);
        CHECK(paths.knownIssues == u8"mcp_docs_dist/KnownIssues.md");
        CHECK(paths.shippingDocsDir.IsEmpty());
    }
    // A later start fills what an earlier one could not.
    {
        editor::mcp::EngineToolPaths paths;
        const String starts[] = {String(u8"mcp_docs_dist/tool"),
                                 String(u8"mcp_docs_checkout/Bin/Debug")};
        editor::mcp::LocateShippingDocs(Span<const String>(starts, 2), paths);
        CHECK(paths.knownIssues == u8"mcp_docs_dist/KnownIssues.md"); // the first hit stands
        CHECK(paths.shippingDocsDir == u8"mcp_docs_checkout/Documentation/Shipping");
    }
    // Nowhere: both fields stay empty and nothing is invented.
    {
        editor::mcp::EngineToolPaths paths;
        const String starts[] = {String(u8"mcp_docs_nowhere/q")};
        editor::mcp::LocateShippingDocs(Span<const String>(starts, 1), paths);
        CHECK(paths.knownIssues.IsEmpty());
        CHECK(paths.shippingDocsDir.IsEmpty());
    }
    std::filesystem::remove_all("mcp_docs_checkout", ec);
    std::filesystem::remove_all("mcp_docs_dist", ec);
}

namespace
{
    // A host whose operations take several pumps: every step answers "not yet" until the
    // configured entry, then the outcome - the shape of the editor's background services,
    // minus the services.
    class SlowOperations final : public editor::mcp::IProjectOperations
    {
    public:
        u32 answerOnEntry = 3;
        u32 cookEntries = 0;
        u32 importEntries = 0;
        u32 exportEntries = 0;
        bool refuseCook = false;
        /// What the last import's options said: its toggles by label.
        Array<String> sawToggles;
        bool sawCollision = false;
        bool sawPrefab = false;

        editor::mcp::OperationStep<editor::mcp::CookOutcome> Cook(bool force) override
        {
            ++cookEntries;
            if (refuseCook)
            {
                return Err(String(u8"a cook is already running (the editor's build lock)"));
            }
            if (cookEntries < answerOnEntry)
            {
                return Optional<editor::mcp::CookOutcome>{};
            }
            editor::mcp::CookOutcome outcome;
            outcome.planned = force ? 7 : 2;
            outcome.cooked = outcome.planned;
            return Optional<editor::mcp::CookOutcome>(outcome);
        }
        editor::mcp::OperationStep<editor::mcp::CreateOutcome>
        Create(const editor::mcp::CreateRequest&) override
        {
            return Err(String(u8"this host creates nothing"));
        }
        editor::mcp::OperationStep<editor::mcp::ImportOutcome>
        Import(const editor::mcp::ImportRequest& request) override
        {
            ++importEntries;
            sawToggles.Clear();
            if (request.options.Get() != nullptr)
            {
                for (const pipeline::ImportOptions::Toggle& toggle : request.options->Toggles())
                {
                    sawToggles.PushBack(String(toggle.label));
                    sawCollision = toggle.label == u8"Generate collision" ? *toggle.value : sawCollision;
                    sawPrefab = toggle.label == u8"Generate prefab" ? *toggle.value : sawPrefab;
                }
            }
            if (importEntries < answerOnEntry)
            {
                return Optional<editor::mcp::ImportOutcome>{};
            }
            editor::mcp::ImportOutcome outcome;
            outcome.name = String(u8"Mover");
            outcome.importer = String(request.importer->Label());
            outcome.deferredWrites = 1;
            return Optional<editor::mcp::ImportOutcome>(Move(outcome));
        }
        editor::mcp::OperationStep<editor::mcp::ExportOutcome>
        Export(const editor::mcp::ExportRequest& request) override
        {
            ++exportEntries;
            if (exportEntries < answerOnEntry)
            {
                return Optional<editor::mcp::ExportOutcome>{};
            }
            editor::mcp::ExportOutcome outcome;
            outcome.result.outputDir = PathJoin(request.outRoot.AsView(), request.preset.name.AsView());
            outcome.result.filesStaged = 1;
            return Optional<editor::mcp::ExportOutcome>(Move(outcome));
        }
    };

    JsonValue ToolCallLine(StringView tool, StringView argumentsJson)
    {
        return json::Parse(Format(u8"{{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\","
                                  u8"\"params\":{{\"name\":\"{}\",\"arguments\":{}}}}}",
                                  tool, argumentsJson)
                               .AsView())
            .value;
    }
}

TEST_CASE("integration.mcp: the write tools ride a host's operations - not finished until the "
          "host says so, then the shared result shape; a refusal is the tool's error")
{
    std::error_code ec;
    std::filesystem::remove_all("mcp_slow_project", ec);
    pipeline::BuilderRegistry builders{DefaultAllocator()};
    pipeline::ImporterRegistry importers{DefaultAllocator()};
    pipeline::RegisterPipelineTypes();
    pipeline::RegisterAllImporters(importers);

    McpServer server;
    editor::mcp::ProjectSession session;
    editor::mcp::ProjectOwner owner;
    SlowOperations slow;
    editor::mcp::RegisterProjectOpenTools(server, session, owner);
    editor::mcp::RegisterAssetWriteTools(server, session, importers, slow);
    editor::mcp::RegisterProjectExportTool(server, session, slow);
    (void)FfCall(server, u8"project_create",
                 FfStr(FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_slow_project"),
                       u8"name", u8"Slow"));
    (void)FfCall(server, u8"project_open",
                 FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_slow_project"));

    // asset_cook: two "not yet" re-entries with the SAME line, then the counts.
    const String cook = ToolCallLine(u8"asset_cook", u8"{\"force\":true}").ToString();
    CHECK(server.HandleLine(cook.AsView()).state == LineState::NotFinished);
    CHECK(server.HandleLine(cook.AsView()).state == LineState::NotFinished);
    LineOutcome cooked = server.HandleLine(cook.AsView());
    REQUIRE(cooked.state == LineState::Answered);
    JsonValue cookResult = json::Parse(cooked.response.AsView()).value.Get(u8"result");
    CHECK(cookResult.Get(u8"isError").AsBool() == false);
    CHECK(JsonValue::Parse(cookResult.Get(u8"content").At(0).Get(u8"text").AsString().AsView())
              .Get(u8"planned")
              .AsInt() == 7);
    CHECK(slow.cookEntries == 3);

    // asset_import: the routing refusal never reaches the operations; a routed file does.
    const String unknown =
        ToolCallLine(u8"asset_import", u8"{\"source\":\"nothing.zzz\"}").ToString();
    LineOutcome refused = server.HandleLine(unknown.AsView());
    REQUIRE(refused.state == LineState::Answered);
    CHECK(json::Parse(refused.response.AsView()).value.Get(u8"result").Get(u8"isError").AsBool());
    CHECK(slow.importEntries == 0);
    const String import = ToolCallLine(u8"asset_import", u8"{\"source\":\"Mover.luau\"}").ToString();
    CHECK(server.HandleLine(import.AsView()).state == LineState::NotFinished);
    CHECK(server.HandleLine(import.AsView()).state == LineState::NotFinished);
    LineOutcome imported = server.HandleLine(import.AsView());
    REQUIRE(imported.state == LineState::Answered);
    JsonValue importResult = json::Parse(imported.response.AsView()).value.Get(u8"result");
    CHECK(importResult.Get(u8"isError").AsBool() == false);
    CHECK(JsonValue::Parse(importResult.Get(u8"content").At(0).Get(u8"text").AsString().AsView())
              .Get(u8"name")
              .AsString() == StringView(u8"Mover"));

    // The importer's options, the import dialog's toggles by label (Sedulous a9c83eef): the
    // host's importer gets them, unnamed ones at their defaults, and the result lists them all.
    {
        slow.importEntries = 0;
        const String model = ToolCallLine(u8"asset_import",
                                          u8"{\"source\":\"Hero.gltf\",\"options\":{\"generate "
                                          u8"Collision\":true}}")
                                 .ToString();
        LineOutcome answer;
        for (u32 pumps = 0; pumps < 5; ++pumps)
        {
            answer = server.HandleLine(model.AsView());
            if (answer.state == LineState::Answered)
            {
                break;
            }
        }
        REQUIRE(answer.state == LineState::Answered);
        JsonValue result = json::Parse(answer.response.AsView()).value.Get(u8"result");
        REQUIRE_FALSE(result.Get(u8"isError").AsBool());
        const JsonValue payload =
            JsonValue::Parse(result.Get(u8"content").At(0).Get(u8"text").AsString().AsView());
        CHECK(payload.Get(u8"options").Get(u8"Generate collision").AsBool());
        CHECK(payload.Get(u8"options").Get(u8"Generate prefab").AsBool()); // a default kept
        CHECK(slow.sawCollision);
        CHECK(slow.sawPrefab);

        const String wings = ToolCallLine(u8"asset_import",
                                          u8"{\"source\":\"Hero.gltf\",\"options\":{\"wings\":true}}")
                                 .ToString();
        answer = server.HandleLine(wings.AsView());
        REQUIRE(answer.state == LineState::Answered);
        const String text = json::Parse(answer.response.AsView())
                                .value.Get(u8"result")
                                .Get(u8"content")
                                .At(0)
                                .Get(u8"text")
                                .AsString();
        CHECK(text.AsView().StartsWith(u8"the Model importer has no option 'wings'; its options are: "));
        CHECK(text.AsView().ContainsIgnoreCase(u8"Generate collision"));
        const String none = ToolCallLine(u8"asset_import",
                                         u8"{\"source\":\"Mover.luau\",\"options\":{\"x\":true}}")
                                .ToString();
        answer = server.HandleLine(none.AsView());
        REQUIRE(answer.state == LineState::Answered);
        CHECK(json::Parse(answer.response.AsView())
                  .value.Get(u8"result")
                  .Get(u8"content")
                  .At(0)
                  .Get(u8"text")
                  .AsString()
                  .AsView()
                  .EndsWith(u8"its options are: none"));
    }

    // project_export: the preset is resolved by the tool (the synthesized host preset here),
    // the work by the operations.
    const String exported = ToolCallLine(u8"project_export", u8"{}").ToString();
    CHECK(server.HandleLine(exported.AsView()).state == LineState::NotFinished);
    CHECK(server.HandleLine(exported.AsView()).state == LineState::NotFinished);
    LineOutcome done = server.HandleLine(exported.AsView());
    REQUIRE(done.state == LineState::Answered);
    JsonValue exportResult = json::Parse(done.response.AsView()).value.Get(u8"result");
    CHECK(exportResult.Get(u8"isError").AsBool() == false);
    CHECK(JsonValue::Parse(exportResult.Get(u8"content").At(0).Get(u8"text").AsString().AsView())
              .Get(u8"filesStaged")
              .AsInt() == 1);
    CHECK(slow.exportEntries == 3);

    // A refusal from the operations is the tool's error text, at once.
    slow.refuseCook = true;
    LineOutcome locked = server.HandleLine(cook.AsView());
    REQUIRE(locked.state == LineState::Answered);
    JsonValue lockedResult = json::Parse(locked.response.AsView()).value.Get(u8"result");
    CHECK(lockedResult.Get(u8"isError").AsBool());
    CHECK(lockedResult.Get(u8"content").At(0).Get(u8"text").AsString().AsView() ==
          StringView(u8"a cook is already running (the editor's build lock)"));
    std::filesystem::remove_all("mcp_slow_project", ec);
}


// agent-playtesting-and-asset-creation.md P2 (Sedulous 14d6d524): asset_creators lists what
// File > New offers, and asset_create makes one by label or by type, under a group or the
// creator's own, with an exact name refused when taken.
TEST_CASE("integration.mcp: asset_creators and asset_create make what File > New makes")
{
    pipeline::RegisterPipelineTypes();
    pipeline::BuilderRegistry builders{DefaultAllocator()};
    pipeline::ImporterRegistry importers{DefaultAllocator()};
    pipeline::AssetCreatorRegistry creators{DefaultAllocator()};
    (void)pipeline::RegisterAllCreators(creators);
    editor::EditorLogBuffer logBuffer{DefaultAllocator()};
    editor::mcp::ProjectSession session;
    editor::mcp::ProjectOwner owner;
    McpServer server;
    editor::mcp::InlineProjectOperations operations(session, builders, String(), String());
    editor::mcp::RegisterEngineTools(server, session, builders, importers, creators, logBuffer,
                                     editor::mcp::EngineToolPaths{}, operations);
    editor::mcp::RegisterProjectOpenTools(server, session, owner);
    (void)RemoveDirectoryRecursive(u8"mcp_create_project");
    (void)FfCall(server, u8"project_create",
                 FfStr(FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_create_project"),
                       u8"name", u8"Create"));
    (void)FfCall(server, u8"project_open",
                 FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_create_project"));

    // The list is the registry, with each creator's default group.
    JsonValue listed = FfCall(server, u8"asset_creators", JsonValue::MakeObject());
    CHECK(listed.Get(u8"count").AsNumber() == doctest::Approx(static_cast<f64>(creators.Count())));
    bool sawMaterials = false;
    for (usize i = 0; i < listed.Get(u8"creators").Items().Size(); ++i)
    {
        const JsonValue& item = listed.Get(u8"creators").At(i);
        if (item.Get(u8"label").AsString() == StringView(u8"PBR Material"))
        {
            sawMaterials = item.Get(u8"defaultGroup").AsString() == StringView(u8"Materials");
        }
    }
    CHECK(sawMaterials);

    // By label (any case), named: it lands in the creator's default group under that name.
    JsonValue scene = FfCall(server, u8"asset_create",
                             FfStr(FfStr(JsonValue::MakeObject(), u8"creator", u8"scene"),
                                   u8"name", u8"Arena"));
    CHECK(scene.Get(u8"name").AsString() == StringView(u8"Arena"));
    CHECK(scene.Get(u8"path").AsString() == StringView(u8"Scenes/Arena"));
    CHECK(scene.Get(u8"type").AsString() == StringView(u8"SceneDocument"));

    // By type, into a group made for it.
    JsonValue map = FfCall(server, u8"asset_create",
                           FfStr(FfStr(JsonValue::MakeObject(), u8"type", u8"InputMapAsset"),
                                 u8"group", u8"Input/Maps"));
    CHECK(map.Get(u8"path").AsString() == StringView(u8"Input/Maps/InputMap"));

    // A taken exact name is refused, a type two creators make needs a label.
    const auto refusal = [&](JsonValue arguments)
    {
        JsonValue params = JsonValue::MakeObject();
        params.Set(u8"name", JsonValue::MakeString(String(u8"asset_create")));
        params.Set(u8"arguments", Move(arguments));
        JsonValue req = JsonValue::MakeObject();
        req.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
        req.Set(u8"id", JsonValue::MakeNumber(2));
        req.Set(u8"method", JsonValue::MakeString(u8"tools/call"));
        req.Set(u8"params", Move(params));
        LineOutcome line = server.HandleLine(req.ToString().AsView());
        JsonValue resp = json::Parse(line.response.AsView()).value;
        CHECK(resp.Get(u8"result").Get(u8"isError").AsBool());
        return String(resp.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString());
    };
    CHECK(refusal(FfStr(FfStr(JsonValue::MakeObject(), u8"creator", u8"Scene"), u8"name",
                        u8"Arena"))
              .AsView()
              .ContainsIgnoreCase(u8"already exists"));
    CHECK(refusal(FfStr(JsonValue::MakeObject(), u8"type", u8"MaterialAsset"))
              .AsView()
              .ContainsIgnoreCase(u8"no single creator"));
    CHECK(refusal(FfStr(JsonValue::MakeObject(), u8"creator", u8"Nope"))
              .AsView()
              .ContainsIgnoreCase(u8"no creator labelled"));

    // Sedulous a700e581: every code an input map stores as a number names its cases in
    // type_info, so an agent editing bindings reads them.
    for (StringView code : {StringView(u8"KeyCode"), StringView(u8"MouseButton"),
                            StringView(u8"GamepadButton"), StringView(u8"GamepadAxis"),
                            StringView(u8"MouseAxisCode"), StringView(u8"StickCode")})
    {
        CAPTURE(String(code).CStr());
        JsonValue info = FfCall(server, u8"type_info", FfStr(JsonValue::MakeObject(), u8"type", code));
        CHECK(info.Get(u8"enum").Count() >= 2);
    }

    owner.project.Reset();
    session.project = nullptr;
    (void)RemoveDirectoryRecursive(u8"mcp_create_project");
}

// Sedulous 3dae9ac8: asset_data_read hands out an asset's envelope; asset_data_write takes an
// edited one back only when it loads, as the engine's own reader reads it.
TEST_CASE("integration.mcp: an agent edits a data asset through its envelope")
{
    pipeline::RegisterPipelineTypes();
    pipeline::BuilderRegistry builders{DefaultAllocator()};
    pipeline::ImporterRegistry importers{DefaultAllocator()};
    pipeline::AssetCreatorRegistry creators{DefaultAllocator()};
    (void)pipeline::RegisterAllCreators(creators);
    editor::EditorLogBuffer logBuffer{DefaultAllocator()};
    editor::mcp::ProjectSession session;
    editor::mcp::ProjectOwner owner;
    McpServer server;
    editor::mcp::InlineProjectOperations operations(session, builders, String(), String());
    editor::mcp::RegisterEngineTools(server, session, builders, importers, creators, logBuffer,
                                     editor::mcp::EngineToolPaths{}, operations);
    editor::mcp::RegisterProjectOpenTools(server, session, owner);
    (void)RemoveDirectoryRecursive(u8"mcp_asset_data");
    (void)FfCall(server, u8"project_create",
                 FfStr(FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_asset_data"), u8"name",
                       u8"Data"));
    (void)FfCall(server, u8"project_open",
                 FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_asset_data"));
    JsonValue made = FfCall(server, u8"asset_create",
                            FfStr(FfStr(JsonValue::MakeObject(), u8"type", u8"InputMapAsset"),
                                  u8"name", u8"Controls"));
    const String guid = made.Get(u8"guid").AsString();
    u32 announced = 0;
    session.onAssetWritten = [&announced](const Guid&) { ++announced; };

    JsonValue read =
        FfCall(server, u8"asset_data_read", FfStr(JsonValue::MakeObject(), u8"guid", guid.AsView()));
    CHECK(read.Get(u8"type").AsString() == StringView(u8"InputMapAsset"));
    const String original = read.Get(u8"xml").AsString();
    REQUIRE(original.AsView().ContainsIgnoreCase(u8"<string name=\"typeName\">InputMapAsset</string>"));
    REQUIRE(original.AsView().ContainsIgnoreCase(u8">Gameplay<"));
    const String edited = ReplaceAll(original.AsView(), u8">Gameplay<", u8">Platformer<");

    const auto write = [&](StringView xml)
    {
        JsonValue arguments = FfStr(FfStr(JsonValue::MakeObject(), u8"guid", guid.AsView()), u8"xml", xml);
        JsonValue params = JsonValue::MakeObject();
        params.Set(u8"name", JsonValue::MakeString(String(u8"asset_data_write")));
        params.Set(u8"arguments", Move(arguments));
        JsonValue req = JsonValue::MakeObject();
        req.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
        req.Set(u8"id", JsonValue::MakeNumber(3));
        req.Set(u8"method", JsonValue::MakeString(u8"tools/call"));
        req.Set(u8"params", Move(params));
        LineOutcome line = server.HandleLine(req.ToString().AsView());
        return json::Parse(line.response.AsView()).value.Get(u8"result");
    };
    const auto refusedWith = [&](StringView xml, StringView reason)
    {
        JsonValue result = write(xml);
        CHECK(result.Get(u8"isError").AsBool());
        const String text = result.Get(u8"content").At(0).Get(u8"text").AsString();
        CAPTURE(reinterpret_cast<const char*>(text.CStr()));
        CHECK(text.AsView().ContainsIgnoreCase(reason));
    };

    // Refusals: each leaves the stored envelope exactly as it was, and announces nothing.
    refusedWith(ReplaceAll(edited.AsView(), guid.AsView(), u8"00000000-0000-0000-0000-000000000001").AsView(),
                u8"is not this asset's");
    refusedWith(ReplaceAll(edited.AsView(), u8"name=\"priority\"", u8"name=\"prio\"").AsView(),
                u8"the payload did not read");
    refusedWith(ReplaceAll(edited.AsView(), u8">InputMapAsset<", u8">SoundCueAsset<").AsView(),
                u8"type");
    CHECK(FfCall(server, u8"asset_data_read", FfStr(JsonValue::MakeObject(), u8"guid", guid.AsView()))
              .Get(u8"xml")
              .AsString() == original.AsView());
    CHECK(announced == 0u);

    JsonValue written = write(edited.AsView());
    REQUIRE_FALSE(written.Get(u8"isError").AsBool());
    CHECK(announced == 1u);
    CHECK(FfCall(server, u8"asset_data_read", FfStr(JsonValue::MakeObject(), u8"guid", guid.AsView()))
              .Get(u8"xml")
              .AsString()
              .AsView()
              .ContainsIgnoreCase(u8">Platformer<"));

    owner.project.Reset();
    session.project = nullptr;
    (void)RemoveDirectoryRecursive(u8"mcp_asset_data");
}

// Sedulous 3de51786: project_settings_set sets what the Project Settings dialog edits, typed by
// the settings' reflection, checked in full first, saved to the manifest, and read back by
// project_info.
TEST_CASE("integration.mcp: an agent sets the project's settings")
{
    pipeline::RegisterPipelineTypes();
    pipeline::BuilderRegistry builders{DefaultAllocator()};
    pipeline::ImporterRegistry importers{DefaultAllocator()};
    pipeline::AssetCreatorRegistry creators{DefaultAllocator()};
    (void)pipeline::RegisterAllCreators(creators);
    editor::EditorLogBuffer logBuffer{DefaultAllocator()};
    editor::mcp::ProjectSession session;
    editor::mcp::ProjectOwner owner;
    McpServer server;
    editor::mcp::InlineProjectOperations operations(session, builders, String(), String());
    editor::mcp::RegisterEngineTools(server, session, builders, importers, creators, logBuffer,
                                     editor::mcp::EngineToolPaths{}, operations);
    editor::mcp::RegisterProjectOpenTools(server, session, owner);
    u32 changed = 0;
    session.onSettingsChanged = [&changed]() { ++changed; };
    (void)RemoveDirectoryRecursive(u8"mcp_settings");
    (void)FfCall(server, u8"project_create",
                 FfStr(FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_settings"), u8"name",
                       u8"Settings"));
    (void)FfCall(server, u8"project_open",
                 FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_settings"));
    const String mapId = FfCall(server, u8"asset_create",
                                FfStr(FfStr(JsonValue::MakeObject(), u8"type", u8"InputMapAsset"),
                                      u8"name", u8"Controls"))
                             .Get(u8"guid")
                             .AsString();
    const String sceneId = FfCall(server, u8"asset_create",
                                  FfStr(FfStr(JsonValue::MakeObject(), u8"creator", u8"Scene"),
                                        u8"name", u8"Level1"))
                               .Get(u8"guid")
                               .AsString();

    const auto refused = [&](JsonValue arguments)
    {
        JsonValue params = JsonValue::MakeObject();
        params.Set(u8"name", JsonValue::MakeString(String(u8"project_settings_set")));
        params.Set(u8"arguments", Move(arguments));
        JsonValue req = JsonValue::MakeObject();
        req.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
        req.Set(u8"id", JsonValue::MakeNumber(4));
        req.Set(u8"method", JsonValue::MakeString(u8"tools/call"));
        req.Set(u8"params", Move(params));
        LineOutcome line = server.HandleLine(req.ToString().AsView());
        JsonValue result = json::Parse(line.response.AsView()).value.Get(u8"result");
        CHECK(result.Get(u8"isError").AsBool());
        return result.Get(u8"content").At(0).Get(u8"text").AsString();
    };

    // A scene where an input map goes is refused, and the scene given beside it with it.
    const String wrongType = refused(FfStr(FfStr(JsonValue::MakeObject(), u8"defaultInputMapId",
                                                 sceneId.AsView()),
                                           u8"defaultSceneId", sceneId.AsView()));
    CHECK(wrongType.AsView().StartsWith(u8"`defaultInputMapId` takes a InputMapAsset; 'Level1' is a"));
    CHECK(refused(FfStr(JsonValue::MakeObject(), u8"defaultMap", mapId.AsView()))
              .AsView()
              .StartsWith(u8"no setting 'defaultMap'; the settings are: name, nativeModule, "));
    JsonValue msaa3 = JsonValue::MakeObject();
    msaa3.Set(u8"renderMsaaSamples", JsonValue::MakeNumber(3));
    CHECK(refused(Move(msaa3)).AsView() == StringView(u8"`renderMsaaSamples` takes 1, 2, 4"));
    CHECK(changed == 0u);
    CHECK(FfCall(server, u8"project_info", JsonValue::MakeObject())
              .Get(u8"settings")
              .Get(u8"defaultSceneId")
              .IsNull()); // nothing changed

    JsonValue arguments = FfStr(FfStr(JsonValue::MakeObject(), u8"defaultInputMapId", mapId.AsView()),
                                u8"defaultSceneId", sceneId.AsView());
    arguments.Set(u8"renderMsaaSamples", JsonValue::MakeNumber(4));
    JsonValue set = FfCall(server, u8"project_settings_set", Move(arguments));
    CHECK(changed == 1u);
    const JsonValue settings = set.Get(u8"settings");
    CHECK(settings.Get(u8"defaultInputMapId").Get(u8"path").AsString().AsView().EndsWith(u8"Controls"));
    CHECK(settings.Get(u8"defaultSceneId").Get(u8"path").AsString() == StringView(u8"Scenes/Level1"));
    CHECK(settings.Get(u8"renderMsaaSamples").AsNumber() == doctest::Approx(4.0));
    CHECK(session.project->Settings().defaultScene == u8"Scenes/Level1"); // the path mirror follows

    // Saved: a reopen reads it from the manifest; then "" clears one and leaves the rest.
    (void)FfCall(server, u8"project_open", FfStr(JsonValue::MakeObject(), u8"directory", u8"mcp_settings"));
    CHECK(FfCall(server, u8"project_info", JsonValue::MakeObject())
              .Get(u8"settings")
              .Get(u8"defaultInputMapId")
              .Get(u8"guid")
              .AsString() == mapId.AsView());
    JsonValue cleared = FfCall(server, u8"project_settings_set",
                               FfStr(JsonValue::MakeObject(), u8"defaultInputMapId", u8""))
                            .Get(u8"settings");
    CHECK(cleared.Get(u8"defaultInputMapId").IsNull());
    CHECK(cleared.Get(u8"defaultSceneId").Get(u8"guid").AsString() == sceneId.AsView());

    owner.project.Reset();
    session.project = nullptr;
    (void)RemoveDirectoryRecursive(u8"mcp_settings");
}
