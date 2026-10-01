// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene - :pie_tools partition (implementation).
module;
#include "Core/Prelude.h"

module editor.scene;

import foundation.core;
import foundation.json;
import foundation.mcp;
import editor.core;

using namespace foundation::core;
using foundation::json::JsonValue;
using foundation::mcp::SchemaBuilder;
using foundation::mcp::ToolAnnotations;
using foundation::mcp::ToolOutcome;

namespace editor
{
    namespace
    {
        constexpr StringView kPieArgument = u8"the PIE instance's id, as pie_list reports it "
                                            u8"(default: the primary, `game-page`)";
        /// Frames: five minutes at 60 Hz, the cook Play waits on included, then pie_start gives
        /// up.
        constexpr u32 kStartPumpLimit = 18000;
        /// Frames: ten seconds at 60 Hz, then pie_screenshot gives up.
        constexpr u32 kCapturePumpLimit = 600;

        /// The pie_start in flight: the tool is re-entered every pump with the same arguments
        /// until the instance answers, or it gives up.
        struct PendingPie
        {
            EditorPage* page = nullptr; // borrowed; null while nothing is in flight
            u32 pumps = 0;
        };

        /// The pie_screenshot calls in flight, one per instance: the HTTP host re-enters every
        /// unfinished call each pump, so captures of two instances interleave.
        struct CaptureWaits
        {
            struct Wait
            {
                EditorPage* page = nullptr; // borrowed
                u32 pumps = 0;
            };
            Array<Wait> waits;
            u32 serial = 0; // per host, so two captures of one instance never share a default name

            Wait* Find(const EditorPage* page) noexcept
            {
                for (Wait& wait : waits)
                {
                    if (wait.page == page)
                    {
                        return &wait;
                    }
                }
                return nullptr;
            }
            void Remove(const EditorPage* page) noexcept
            {
                for (usize i = 0; i < waits.Size(); ++i)
                {
                    if (waits[i].page == page)
                    {
                        waits.RemoveAt(i);
                        return;
                    }
                }
            }
        };

        IPieInstancePage* PieOf(EditorPage* page) noexcept
        {
            return ServiceOf<IPieInstancePage>(page);
        }

        bool IsOpen(const EditorContext& context, const EditorPage* page)
        {
            for (const UniquePtr<EditorPage>& open : context.OpenPages())
            {
                if (open.Get() == page)
                {
                    return true;
                }
            }
            return false;
        }

        /// The open Game page with this PIE id, or null.
        EditorPage* FindPie(const EditorContext& context, StringView id)
        {
            for (const UniquePtr<EditorPage>& open : context.OpenPages())
            {
                IPieInstancePage* pie = PieOf(open.Get());
                if (pie != nullptr && pie->PieId() == id)
                {
                    return open.Get();
                }
            }
            return nullptr;
        }

        ToolOutcome Start(EditorContext& context, PendingPie& pending, const JsonValue& args)
        {
            if (pending.page != nullptr)
            {
                // Re-entered: the same call, one pump later.
                EditorPage* page = pending.page;
                ++pending.pumps;
                if (!IsOpen(context, page))
                {
                    pending.page = nullptr;
                    return Err(String(u8"the Game tab closed while starting"));
                }
                IPieInstancePage* pie = PieOf(page);
                if (pie->IsRunning() && pie->FrameCount() > 0)
                {
                    pending.page = nullptr;
                    JsonValue out = PieStateJson(*pie);
                    out.Set(u8"alreadyRunning", JsonValue::MakeBool(false));
                    return out;
                }
                if (!pie->IsRunning() && !pie->IsStarting())
                {
                    pending.page = nullptr;
                    return Err(Format(u8"PIE instance '{}' did not start (log_read says why)",
                                      pie->PieId()));
                }
                if (pending.pumps > kStartPumpLimit)
                {
                    pending.page = nullptr;
                    return Err(Format(u8"PIE instance '{}' rendered no frame in five minutes - a "
                                      u8"cook still running, or its tab hidden (an editor window "
                                      u8"minimised)?",
                                      pie->PieId()));
                }
                return ToolOutcome::NotFinished();
            }

            const bool newInstance = args.Get(u8"newInstance").AsBool();
            if (!newInstance)
            {
                if (EditorPage* primary = FindPie(context, kPrimaryPieId))
                {
                    IPieInstancePage* pie = PieOf(primary);
                    if (pie->IsRunning())
                    {
                        JsonValue out = PieStateJson(*pie);
                        out.Set(u8"alreadyRunning", JsonValue::MakeBool(true));
                        return out;
                    }
                }
            }

            // The editor's own Play path opens (or reveals) the tab; the new one is the Game
            // page that was not open before.
            Array<EditorPage*> before;
            for (const UniquePtr<EditorPage>& open : context.OpenPages())
            {
                if (PieOf(open.Get()) != nullptr)
                {
                    before.PushBack(open.Get());
                }
            }
            const StringView actionId = newInstance ? StringView(u8"game.playNewInstance")
                                                    : StringView(u8"game.play");
            const Status executed = context.Actions().Execute(actionId);
            if (executed.Code() == ErrorCode::NotSupported)
            {
                return Err(Format(u8"the editor refused '{}': it plays only with a project open",
                                  actionId));
            }
            if (!executed.IsOk())
            {
                return Err(Format(u8"the editor has no '{}' action", actionId));
            }
            EditorPage* target = nullptr;
            for (const UniquePtr<EditorPage>& open : context.OpenPages())
            {
                IPieInstancePage* pie = PieOf(open.Get());
                if (pie == nullptr)
                {
                    continue;
                }
                bool wasOpen = false;
                for (EditorPage* old : before)
                {
                    wasOpen = wasOpen || old == open.Get();
                }
                if (newInstance ? !wasOpen : pie->PieId() == kPrimaryPieId)
                {
                    target = open.Get();
                    break;
                }
            }
            if (target == nullptr)
            {
                return Err(String(u8"no Game tab opened: this editor build has no play-in-editor"));
            }
            PieOf(target)->Play();
            pending.page = target;
            pending.pumps = 0;
            return ToolOutcome::NotFinished();
        }

        ToolOutcome Screenshot(EditorContext& context, CaptureWaits& captures, const JsonValue& args)
        {
            // A wait whose tab closed is over; its page pointer is never used again.
            for (usize i = captures.waits.Size(); i > 0; --i)
            {
                if (!IsOpen(context, captures.waits[i - 1].page))
                {
                    captures.waits.RemoveAt(i - 1);
                }
            }
            Result<EditorPage*, String> resolved = ResolvePie(context, args);
            if (!resolved.HasValue())
            {
                return Err(Move(resolved.Error()));
            }
            EditorPage* page = resolved.Value();
            IPieInstancePage* pie = PieOf(page);
            if (CaptureWaits::Wait* wait = captures.Find(page))
            {
                // Re-entered: the same call, one pump later.
                const ViewportCapture& capture = pie->LastViewportCapture();
                ++wait->pumps;
                if (capture.state == ViewportCaptureState::Written)
                {
                    captures.Remove(page);
                    JsonValue out = JsonValue::MakeObject();
                    out.Set(u8"pie", JsonValue::MakeString(String(pie->PieId())));
                    out.Set(u8"path", JsonValue::MakeString(capture.path));
                    out.Set(u8"width", JsonValue::MakeNumber(static_cast<f64>(capture.width)));
                    out.Set(u8"height", JsonValue::MakeNumber(static_cast<f64>(capture.height)));
                    return out;
                }
                if (capture.state == ViewportCaptureState::Failed)
                {
                    captures.Remove(page);
                    return Err(Format(u8"the capture of PIE instance '{}' failed (log_read, "
                                      u8"category Screenshot, says why)",
                                      pie->PieId()));
                }
                if (!pie->IsRunning())
                {
                    captures.Remove(page);
                    return Err(Format(u8"PIE instance '{}' stopped before a frame was captured",
                                      pie->PieId()));
                }
                if (wait->pumps > kCapturePumpLimit)
                {
                    captures.Remove(page);
                    return Err(Format(u8"PIE instance '{}' rendered no frame in ten seconds - is "
                                      u8"its tab visible (an editor window minimised or hidden)?",
                                      pie->PieId()));
                }
                return ToolOutcome::NotFinished();
            }
            if (!pie->IsRunning())
            {
                return Err(Format(u8"PIE instance '{}' is not running (pie_start runs it)",
                                  pie->PieId()));
            }
            String path = args.Get(u8"path").AsString();
            if (path.IsEmpty())
            {
                const String dir = PathJoin(GetUserDataDirectory().AsView(), u8"screenshots");
                if (!CreateDirectories(dir.AsView()))
                {
                    return Err(Format(u8"could not create '{}'", dir.AsView()));
                }
                ++captures.serial;
                path = PathJoin(dir.AsView(), Format(u8"{}-{}-{}.png", pie->PieId(), ProcessId(),
                                                     captures.serial)
                                                  .AsView());
            }
            context.RevealPage(page); // to front: a background tab's viewport never renders
            pie->RequestViewportCapture(path.AsView());
            captures.waits.PushBack(CaptureWaits::Wait{page, 0});
            return ToolOutcome::NotFinished();
        }
    }

    JsonValue PieStateJson(const IPieInstancePage& pie)
    {
        JsonValue out = JsonValue::MakeObject();
        out.Set(u8"pie", JsonValue::MakeString(String(pie.PieId())));
        out.Set(u8"running", JsonValue::MakeBool(pie.IsRunning()));
        out.Set(u8"starting", JsonValue::MakeBool(pie.IsStarting()));
        out.Set(u8"scene", JsonValue::MakeString(String(pie.SceneName())));
        out.Set(u8"runTime", JsonValue::MakeNumber(pie.RunTime()));
        out.Set(u8"frames", JsonValue::MakeNumber(static_cast<f64>(pie.FrameCount())));
        JsonValue script = JsonValue::MakeObject();
        switch (pie.ScriptState())
        {
        case PieScriptState::None:
            script.Set(u8"state", JsonValue::MakeString(u8"none"));
            break;
        case PieScriptState::Running:
            script.Set(u8"state", JsonValue::MakeString(u8"running"));
            break;
        case PieScriptState::Faulted:
            script.Set(u8"state", JsonValue::MakeString(u8"faulted"));
            script.Set(u8"fault", JsonValue::MakeString(String(pie.ScriptFault())));
            break;
        }
        out.Set(u8"script", Move(script));
        return out;
    }

    Result<EditorPage*, String> ResolvePie(const EditorContext& context, const JsonValue& args)
    {
        const String asked = args.Get(u8"pie").AsString();
        const StringView id = asked.IsEmpty() ? kPrimaryPieId : asked.AsView();
        if (EditorPage* page = FindPie(context, id))
        {
            return page;
        }
        if (id == kPrimaryPieId)
        {
            return Err(String(u8"no Game tab is open (pie_start opens and runs one)"));
        }
        return Err(Format(u8"no PIE instance '{}' (pie_list names them)", id));
    }

    void RegisterPieTools(foundation::mcp::McpServer& server, EditorContext& context)
    {
        EditorContext* ctx = &context;

        auto starting = MakeUnique<PendingPie>(context.Allocator());
        PendingPie* startingPtr = starting.Get();
        server.RegisterTool(
            u8"pie_start",
            u8"Start playing the project in the editor (PIE): the primary Game tab, or with "
            u8"`newInstance` another tab running an instance of its own - a host and a client, "
            u8"say. Cooks first, as Play does, loads the default scene and the startup script, and "
            u8"answers once the instance's first frame has rendered, with its state as pie_state "
            u8"gives it; `pie` is the id every other PIE tool takes. A primary already running is "
            u8"answered as it is, with `alreadyRunning`. A run that fails to start (a default "
            u8"scene that does not load) is an error; log_read says why.",
            SchemaBuilder()
                .Boolean(u8"newInstance", u8"open another Game tab with an instance of its own "
                                          u8"(Play New Instance) instead of the primary")
                .Build(),
            ToolAnnotations::Creates(),
            [ctx, startingPtr, keep = Move(starting)](const JsonValue& args) -> ToolOutcome
            { return Start(*ctx, *startingPtr, args); });

        server.RegisterTool(
            u8"pie_stop",
            u8"Stop one PIE instance's run (or with `all`, every one's); its tab stays open, and "
            u8"the others keep running. Returns the ids stopped.",
            SchemaBuilder()
                .Str(u8"pie", kPieArgument)
                .Boolean(u8"all", u8"stop every instance")
                .Build(),
            ToolAnnotations::Adjusts(),
            [ctx](const JsonValue& args) -> ToolOutcome
            {
                JsonValue stopped = JsonValue::MakeArray();
                const auto stop = [&stopped](IPieInstancePage& pie)
                {
                    if (pie.IsRunning() || pie.IsStarting())
                    {
                        stopped.Add(JsonValue::MakeString(String(pie.PieId())));
                    }
                    pie.Stop();
                };
                if (args.Get(u8"all").AsBool())
                {
                    for (const UniquePtr<EditorPage>& open : ctx->OpenPages())
                    {
                        if (IPieInstancePage* pie = PieOf(open.Get()))
                        {
                            stop(*pie);
                        }
                    }
                }
                else
                {
                    Result<EditorPage*, String> resolved = ResolvePie(*ctx, args);
                    if (!resolved.HasValue())
                    {
                        return Err(Move(resolved.Error()));
                    }
                    stop(*PieOf(resolved.Value()));
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"stopped", Move(stopped));
                return out;
            });

        server.RegisterTool(
            u8"pie_state",
            u8"One PIE instance's state: whether it is running (or starting, waiting on the cook), "
            u8"the scene it is in, `runTime`, the seconds of frames since it started (unscaled: a "
            u8"menu that stops gameplay time does not stop it), the frames rendered since, and its "
            u8"startup script's state (`none`, `running`, or `faulted` with the reason).",
            SchemaBuilder().Str(u8"pie", kPieArgument).Build(), ToolAnnotations::ReadOnly(),
            [ctx](const JsonValue& args) -> ToolOutcome
            {
                Result<EditorPage*, String> resolved = ResolvePie(*ctx, args);
                if (!resolved.HasValue())
                {
                    return Err(Move(resolved.Error()));
                }
                return PieStateJson(*PieOf(resolved.Value()));
            });

        server.RegisterTool(
            u8"pie_list",
            u8"Every open PIE instance, each with its state as pie_state gives it: what the user "
            u8"started as well as what an agent did.",
            SchemaBuilder().Build(), ToolAnnotations::ReadOnly(),
            [ctx](const JsonValue&) -> ToolOutcome
            {
                JsonValue items = JsonValue::MakeArray();
                for (const UniquePtr<EditorPage>& open : ctx->OpenPages())
                {
                    if (IPieInstancePage* pie = PieOf(open.Get()))
                    {
                        items.Add(PieStateJson(*pie));
                    }
                }
                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"count", JsonValue::MakeNumber(static_cast<f64>(items.Count())));
                out.Set(u8"instances", Move(items));
                return out;
            });

        auto capturing = MakeUnique<CaptureWaits>(context.Allocator());
        CaptureWaits* capturingPtr = capturing.Get();
        server.RegisterTool(
            u8"pie_screenshot",
            u8"What one running PIE instance's Game tab renders, as a PNG at the viewport's size: "
            u8"the game through its own camera, with its UI and overlays. Brings the tab to front "
            u8"(a hidden viewport never renders), waits for the next frame and the GPU, then "
            u8"returns {pie, path, width, height}; read the file. `path` is where to write (an "
            u8"existing directory; default: <user-data>/screenshots/<pie>-<pid>-<n>.png). Refused "
            u8"for a stopped instance; gives up after ten seconds without a rendered frame.",
            SchemaBuilder()
                .Str(u8"pie", kPieArgument)
                .Str(u8"path", u8"the PNG to write (default: a new file under <user-data>/screenshots)")
                .Build(),
            ToolAnnotations::Creates(),
            [ctx, capturingPtr, keep = Move(capturing)](const JsonValue& args) -> ToolOutcome
            { return Screenshot(*ctx, *capturingPtr, args); });

        RegisterPieRunTool(server, context);
    }
}
