// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The tracked sample project (SampleProjects/PaperKid) must stay readable at the CURRENT data
// versions. A wire-version bump or a key rename that forgets to upgrade its sources leaves it
// refused by the strict readers - the cook logged 13 refusals (font, six meshes, six UI
// documents) before the 2026-09-18 upgrade. This registers the whole pipeline the way
// Tools.Cook does, opens a scratch copy (so opening never writes into the tracked tree), and
// reads every instance back.
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include "Core/Prelude.h"
import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.json;
import foundation.mcp;
import pipeline.core;
import pipeline.importer;
import pipeline.registration;
import engine.scenesurface;
import editor.core;
import editor.mcp;

using namespace foundation::core;
using foundation::json::JsonValue;

namespace
{
    // One tools/call round trip through the MCP server; the result must not be an error.
    JsonValue Call(foundation::mcp::McpServer& server, StringView tool, JsonValue arguments)
    {
        JsonValue params = JsonValue::MakeObject();
        params.Set(u8"name", JsonValue::MakeString(String(tool)));
        params.Set(u8"arguments", Move(arguments));
        JsonValue request = JsonValue::MakeObject();
        request.Set(u8"jsonrpc", JsonValue::MakeString(u8"2.0"));
        request.Set(u8"id", JsonValue::MakeNumber(1));
        request.Set(u8"method", JsonValue::MakeString(u8"tools/call"));
        request.Set(u8"params", Move(params));
        Optional<String> line = server.HandleLine(request.ToString().AsView());
        REQUIRE(line.HasValue());
        JsonValue response = foundation::json::Parse(line.Value().AsView()).value;
        REQUIRE(response.Has(u8"result"));
        REQUIRE(response.Get(u8"result").Get(u8"isError").AsBool() == false);
        return foundation::json::Parse(
                   response.Get(u8"result").Get(u8"content").At(0).Get(u8"text").AsString().AsView())
            .value;
    }

    // Every instance under a group (recursively) reads back; fails the case on a refusal.
    usize ReadAllInstances(foundation::content::Group& group)
    {
        usize read = 0;
        for (foundation::content::Instance* instance : group.Instances())
        {
            const RefPtr<ISerializable> object = instance->ReadObject();
            const String name(instance->Name());
            const std::string shown(reinterpret_cast<const char*>(name.CStr()), name.Size());
            CHECK_MESSAGE(object.Get() != nullptr, "sample project instance refused: ", shown);
            ++read;
        }
        for (foundation::content::Group* child : group.Groups())
        {
            read += ReadAllInstances(*child);
        }
        return read;
    }
}

TEST_CASE("sample project: every PaperKid source reads at the CURRENT data versions")
{
    pipeline::RegisterPipelineTypes(); // every asset/product/resource type (idempotent)
    engine::RegisterAllSceneComponentReflection();
    // The readers log WHY they refuse; put that on the console so a red run names the record.
    ConsoleSink console;
    GlobalLogger().AddSink(&console);

    const String dataRoot = foundation::vfs::FindDataRoot();
    REQUIRE_FALSE(dataRoot.IsEmpty());
    const String source = PathJoin(PathParent(dataRoot.AsView()), u8"SampleProjects/PaperKid");
    REQUIRE(DirectoryExists(source.AsView()));

    const std::filesystem::path scratch = "scratch_paperkid_versions";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::copy(std::filesystem::path(reinterpret_cast<const char*>(source.CStr())),
                          scratch, std::filesystem::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    {
        UniquePtr<editor::EditorProject> project =
            editor::EditorProject::Open(DefaultAllocator(), u8"scratch_paperkid_versions");
        REQUIRE(static_cast<bool>(project));
        const usize read = ReadAllInstances(*project->SourceDb().RootGroup());
        // font, input map, bus layout, 6 meshes, 2 scenes + 1 prefab, 5 scripts, 6 UI documents
        CHECK(read >= 23u);
    }

    // And it COOKS, through the same tools an agent uses: the driver builds items in parallel
    // on job workers, which is where the per-build registrations (core types, markup, script
    // facades) used to race - a double free that aborted Tools.Cook on this very project.
    {
        pipeline::BuilderRegistry builders{DefaultAllocator()};
        pipeline::RegisterAllBuilders(builders);
        pipeline::ImporterRegistry importers{DefaultAllocator()};
        pipeline::RegisterAllImporters(importers);
        foundation::mcp::McpServer server;
        editor::mcp::ProjectSession session;
        editor::mcp::RegisterProjectTools(server, session);
        editor::mcp::RegisterAssetTools(server, session);
        editor::mcp::RegisterAssetWriteTools(server, session, builders, importers);
        JsonValue open = JsonValue::MakeObject();
        open.Set(u8"directory", JsonValue::MakeString(u8"scratch_paperkid_versions"));
        (void)Call(server, u8"project_open", Move(open));
        JsonValue args = JsonValue::MakeObject();
        args.Set(u8"force", JsonValue::MakeBool(true));
        const JsonValue cooked = Call(server, u8"asset_cook", Move(args));
        CHECK(cooked.Get(u8"cooked").AsNumber() >= 20.0);
        CHECK(cooked.Get(u8"failed").AsNumber() == doctest::Approx(0.0));
    }
    std::filesystem::remove_all(scratch, ec);
    GlobalLogger().RemoveSink(&console);
}
