// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene - pie_run, the playtest primitive (agent-playtesting-and-asset-creation.md P4).
//
// One call plays a device-level input timeline into one running PIE instance for a stretch of
// run time, and answers what happened: probe samples (entity fields and game script properties)
// at the asked times, screenshots at the asked times, and whether an `until` condition ended it
// early. The call is re-entered every pump until the run ends. One run per instance at a time:
// the run is found again by its instance, so instances run side by side, a host and a client
// scripted over the same seconds.
module;
#include "Core/Prelude.h"

module editor.scene;

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.input;
import foundation.scene;
import editor.core;

using namespace foundation::core;
using foundation::json::JsonValue;
using foundation::mcp::SchemaBuilder;
using foundation::mcp::ToolAnnotations;
using foundation::mcp::ToolOutcome;
namespace input = foundation::input;
namespace scene = foundation::scene;

namespace editor
{
    namespace
    {
        /// The longest run one call takes, in run seconds.
        constexpr f64 kMaxDuration = 600.0;
        /// The sampling period when probes are given without `every` or `sampleAt`.
        constexpr f64 kDefaultEvery = 0.5;
        /// The most samples one run takes.
        constexpr f64 kMaxSamples = 5000.0;

        /// One value a run reads: a field of an entity, or a property of the game script.
        struct Probe
        {
            String entity; ///< a guid, a name or a slash path; empty for a script probe
            String field;
            String script; ///< the game script property; empty for an entity probe
            String label;
        };

        struct Run
        {
            EditorPage* page = nullptr; // borrowed; the run is dropped when the page closes
            f64 start = 0.0;
            u64 startFrames = 0;
            f64 duration = 0.0;
            Array<f64> sampleTimes;
            usize nextSample = 0;
            f64 lastSampleTime = -1.0;
            Array<Probe> probes;
            bool hasUntil = false;
            Probe until;
            String untilOp;
            JsonValue untilValue;
            Array<f64> shotTimes;
            usize nextShot = 0;
            bool shotInFlight = false;
            f64 shotAt = 0.0;
            String shotDirectory;
            u32 serial = 0;
            JsonValue samples = JsonValue::MakeArray();
            JsonValue shots = JsonValue::MakeArray();
            JsonValue untilHit;
            usize inputCount = 0;
            Stopwatch clock;
        };

        struct Runs
        {
            Array<Run> active;
            u32 serial = 0;
        };

        bool IsOpenPage(const EditorContext& context, const EditorPage* page)
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

        void SortTimes(Array<f64>& times)
        {
            times.Sort([](f64 a, f64 b) { return a < b; });
        }

        // ---- The timeline ----

        Result<Float2, String> ReadPair(const JsonValue& value, usize i, StringView name)
        {
            if (!value.IsArray() || value.Count() != 2 || !value.At(0).IsNumber() ||
                !value.At(1).IsNumber())
            {
                return Err(Format(u8"input[{}]: `{}` takes [x, y]", i, name));
            }
            return Float2{static_cast<f32>(value.At(0).AsNumber()),
                          static_cast<f32>(value.At(1).AsNumber())};
        }

        Result<input::ScriptedInput, String> ParseInput(const JsonValue& entry, usize i)
        {
            input::ScriptedInput out;
            if (!entry.IsObject())
            {
                return Err(Format(u8"input[{}]: an object", i));
            }
            const JsonValue at = entry.Get(u8"at");
            if (!at.IsNumber() || at.AsNumber() < 0.0)
            {
                return Err(Format(u8"input[{}]: `at` takes run seconds from 0", i));
            }
            out.at = at.AsNumber();
            out.down = entry.Get(u8"down").AsBool(true);
            out.gamepad = static_cast<i32>(entry.Get(u8"gamepad").AsInt(0));

            // The names are the enums' reflected case names, any case; type_info lists them.
            if (entry.Has(u8"key"))
            {
                const String name = entry.Get(u8"key").AsString();
                out.kind = input::ScriptedInputKind::Key;
                if (!input::ScriptedInputSource::ParseKey(name.AsView(), out.key))
                {
                    return Err(Format(u8"input[{}]: no key '{}' (the KeyCode case names: "
                                      u8"type_info KeyCode lists them)",
                                      i, name.AsView()));
                }
                return out;
            }
            if (entry.Has(u8"mouseButton"))
            {
                const String name = entry.Get(u8"mouseButton").AsString();
                out.kind = input::ScriptedInputKind::MouseButton;
                if (!input::ScriptedInputSource::ParseMouseButton(name.AsView(), out.button))
                {
                    return Err(Format(u8"input[{}]: no mouse button '{}' (type_info MouseButton "
                                      u8"lists them)",
                                      i, name.AsView()));
                }
                return out;
            }
            if (entry.Has(u8"mouseMove") || entry.Has(u8"wheel"))
            {
                const bool move = entry.Has(u8"mouseMove");
                const StringView name = move ? StringView(u8"mouseMove") : StringView(u8"wheel");
                out.kind = move ? input::ScriptedInputKind::MouseMove
                                : input::ScriptedInputKind::MouseWheel;
                Result<Float2, String> pair = ReadPair(entry.Get(String(name)), i, name);
                if (!pair.HasValue())
                {
                    return Err(Move(pair.Error()));
                }
                out.x = pair.Value().x;
                out.y = pair.Value().y;
                return out;
            }
            if (entry.Has(u8"button"))
            {
                const String name = entry.Get(u8"button").AsString();
                out.kind = input::ScriptedInputKind::PadButton;
                if (!input::ScriptedInputSource::ParsePadButton(name.AsView(), out.padButton))
                {
                    return Err(Format(u8"input[{}]: no gamepad button '{}' (type_info "
                                      u8"GamepadButton lists them)",
                                      i, name.AsView()));
                }
                return out;
            }
            if (entry.Has(u8"axis"))
            {
                const String name = entry.Get(u8"axis").AsString();
                out.kind = input::ScriptedInputKind::PadAxis;
                if (!input::ScriptedInputSource::ParsePadAxis(name.AsView(), out.padAxis))
                {
                    return Err(Format(u8"input[{}]: no gamepad axis '{}' (type_info GamepadAxis "
                                      u8"lists them)",
                                      i, name.AsView()));
                }
                const JsonValue value = entry.Get(u8"value");
                if (!value.IsNumber() || value.AsNumber() < -1.0 || value.AsNumber() > 1.0)
                {
                    return Err(Format(u8"input[{}]: an axis takes `value` from -1 to 1", i));
                }
                out.value = static_cast<f32>(value.AsNumber());
                return out;
            }
            return Err(Format(u8"input[{}]: name one of `key`, `mouseButton`, `mouseMove`, "
                              u8"`wheel`, `button` or `axis`",
                              i));
        }

        // ---- Probes ----

        Result<JsonValue, String> ProbeValue(IPieInstancePage& pie, const Probe& probe)
        {
            if (!probe.script.IsEmpty())
            {
                Result<Variant, ErrorCode> value = pie.GetScriptProperty(probe.script.AsView());
                if (!value.HasValue())
                {
                    return Err(Format(u8"the game script of PIE instance '{}' has no property "
                                      u8"'{}' (or no script runs)",
                                      pie.PieId(), probe.script.AsView()));
                }
                return ValueJson(value.Value());
            }
            scene::Scene* running = pie.RunningScene();
            if (running == nullptr)
            {
                return Err(Format(u8"PIE instance '{}' has no scene", pie.PieId()));
            }
            const scene::EntityHandle handle = FindEntity(*running, probe.entity.AsView());
            if (!running->IsValid(handle))
            {
                return Err(Format(u8"no entity '{}' in PIE instance '{}''s scene '{}'",
                                  probe.entity.AsView(), pie.PieId(), running->Name()));
            }
            return EntityFieldJson(*running, handle, probe.entity.AsView(), probe.field.AsView());
        }

        Status ParseProbes(const JsonValue& entry, usize i, Array<Probe>& out, String& error)
        {
            if (!entry.IsObject())
            {
                error = Format(u8"probes[{}]: an object", i);
                return ErrorCode::InvalidArgument;
            }
            if (entry.Has(u8"script"))
            {
                Probe probe;
                probe.script = entry.Get(u8"script").AsString();
                probe.label = Format(u8"script.{}", probe.script.AsView());
                out.PushBack(Move(probe));
                return Status{};
            }
            const String entity = entry.Get(u8"entity").AsString();
            if (entity.IsEmpty())
            {
                error = Format(u8"probes[{}]: name an `entity` (guid, name or slash path) or a "
                               u8"`script` property",
                               i);
                return ErrorCode::InvalidArgument;
            }
            const JsonValue fields = entry.Get(u8"fields");
            const bool hasFields = entry.Has(u8"fields");
            if (hasFields && (!fields.IsArray() || fields.Count() == 0))
            {
                error = Format(u8"probes[{}]: `fields` takes a non-empty array of paths", i);
                return ErrorCode::InvalidArgument;
            }
            const i64 count = hasFields ? fields.Count() : 1;
            for (i64 f = 0; f < count; ++f)
            {
                Probe probe;
                probe.entity = entity;
                probe.field = hasFields ? fields.At(f).AsString() : String(u8"worldPosition");
                probe.label = Format(u8"{}.{}", probe.entity.AsView(), probe.field.AsView());
                out.PushBack(Move(probe));
            }
            return Status{};
        }

        Status ParseUntil(const JsonValue& until, Run& run, String& error)
        {
            if (!until.IsObject())
            {
                error = String(u8"`until` takes {entity, field, op, value} or {script, op, value}");
                return ErrorCode::InvalidArgument;
            }
            Probe& probe = run.until;
            run.hasUntil = true;
            if (until.Has(u8"script"))
            {
                probe.script = until.Get(u8"script").AsString();
                probe.label = Format(u8"script.{}", probe.script.AsView());
            }
            else
            {
                probe.entity = until.Get(u8"entity").AsString();
                if (probe.entity.IsEmpty())
                {
                    error = String(u8"`until` names an `entity` (with a `field`) or a `script` "
                                   u8"property");
                    return ErrorCode::InvalidArgument;
                }
                const JsonValue field = until.Get(u8"field");
                probe.field = field.IsString() ? field.AsString() : String(u8"worldPosition");
                probe.label = Format(u8"{}.{}", probe.entity.AsView(), probe.field.AsView());
            }
            const String op = until.Get(u8"op").AsString();
            const StringView opView = op.AsView();
            if (opView != u8"<" && opView != u8"<=" && opView != u8">" && opView != u8">=" &&
                opView != u8"==" && opView != u8"!=")
            {
                error = String(u8"`until.op` takes <, <=, >, >=, == or !=");
                return ErrorCode::InvalidArgument;
            }
            run.untilOp = op;
            const JsonValue value = until.Get(u8"value");
            if (!(value.IsNumber() || value.IsBool() || value.IsString()))
            {
                error = String(u8"`until.value` takes a number (or, with == and !=, a boolean or "
                               u8"a string)");
                return ErrorCode::InvalidArgument;
            }
            if (!value.IsNumber() && opView != u8"==" && opView != u8"!=")
            {
                error = String(u8"`until` compares a boolean or a string with == or != only");
                return ErrorCode::InvalidArgument;
            }
            run.untilValue = value;
            return Status{};
        }

        /// Whether `value` stands in `op` to `target`: numbers compare, anything else is equal or
        /// not by its JSON text.
        bool Crosses(const JsonValue& value, StringView op, const JsonValue& target)
        {
            if (value.IsNumber() && target.IsNumber())
            {
                const f64 a = value.AsNumber();
                const f64 b = target.AsNumber();
                if (op == u8"<")
                {
                    return a < b;
                }
                if (op == u8"<=")
                {
                    return a <= b;
                }
                if (op == u8">")
                {
                    return a > b;
                }
                if (op == u8">=")
                {
                    return a >= b;
                }
                if (op == u8"==")
                {
                    return a == b;
                }
                return a != b;
            }
            const String a = value.ToString();
            const String b = target.ToString();
            if (op == u8"==")
            {
                return a == b;
            }
            if (op == u8"!=")
            {
                return a != b;
            }
            return false;
        }

        Status SampleTimes(const JsonValue& args, Run& run, String& error)
        {
            if (args.Has(u8"sampleAt"))
            {
                const JsonValue at = args.Get(u8"sampleAt");
                if (!at.IsArray() || at.Count() == 0)
                {
                    error = String(u8"`sampleAt` takes a non-empty array of run times");
                    return ErrorCode::InvalidArgument;
                }
                for (i64 i = 0; i < at.Count(); ++i)
                {
                    const JsonValue time = at.At(i);
                    if (!time.IsNumber() || time.AsNumber() < 0.0 ||
                        time.AsNumber() > run.duration)
                    {
                        error = Format(u8"sampleAt[{}]: a run time from 0 to the duration ({})",
                                       i, run.duration);
                        return ErrorCode::InvalidArgument;
                    }
                    run.sampleTimes.PushBack(time.AsNumber());
                }
                SortTimes(run.sampleTimes);
                return Status{};
            }
            f64 every = kDefaultEvery;
            if (args.Has(u8"every"))
            {
                const JsonValue everyArg = args.Get(u8"every");
                if (!everyArg.IsNumber() || everyArg.AsNumber() <= 0.0)
                {
                    error = String(u8"`every` takes run seconds above 0");
                    return ErrorCode::InvalidArgument;
                }
                every = everyArg.AsNumber();
            }
            if (run.duration / every > kMaxSamples)
            {
                error = Format(u8"`every` {} over {} seconds is more than {} samples", every,
                               run.duration, kMaxSamples);
                return ErrorCode::InvalidArgument;
            }
            for (u32 i = 0; static_cast<f64>(i) * every <= run.duration; ++i)
            {
                run.sampleTimes.PushBack(static_cast<f64>(i) * every);
            }
            return Status{};
        }

        void TakeSample(IPieInstancePage& pie, Run& run, f64 t)
        {
            JsonValue row = JsonValue::MakeObject();
            row.Set(u8"t", JsonValue::MakeNumber(t));
            row.Set(u8"frame",
                    JsonValue::MakeNumber(static_cast<f64>(pie.FrameCount() - run.startFrames)));
            JsonValue values = JsonValue::MakeObject();
            for (const Probe& probe : run.probes)
            {
                Result<JsonValue, String> value = ProbeValue(pie, probe);
                // An entity gone by now reads null; the run goes on.
                values.Set(probe.label, value.HasValue() ? Move(value.Value())
                                                         : JsonValue::MakeNull());
            }
            row.Set(u8"values", Move(values));
            run.samples.Add(Move(row));
            run.lastSampleTime = t;
        }

        ToolOutcome Finish(Runs& runs, usize index, StringView endedBy, f64 t)
        {
            Run& run = runs.active[index];
            IPieInstancePage& pie = *ServiceOf<IPieInstancePage>(run.page);
            if (pie.IsRunning() && !run.probes.IsEmpty() && run.lastSampleTime < t)
            {
                TakeSample(pie, run, t);
            }
            pie.EndScriptedInput();
            JsonValue out = JsonValue::MakeObject();
            out.Set(u8"pie", JsonValue::MakeString(String(pie.PieId())));
            out.Set(u8"endedBy", JsonValue::MakeString(String(endedBy)));
            out.Set(u8"runTime", JsonValue::MakeNumber(t));
            out.Set(u8"frames",
                    JsonValue::MakeNumber(static_cast<f64>(pie.FrameCount() - run.startFrames)));
            out.Set(u8"inputs", JsonValue::MakeNumber(static_cast<f64>(run.inputCount)));
            out.Set(u8"samples", Move(run.samples));
            out.Set(u8"screenshots", Move(run.shots));
            out.Set(u8"until", Move(run.untilHit));
            out.Set(u8"state", PieStateJson(pie));
            runs.active.RemoveAt(index);
            return out;
        }

        ToolOutcome Step(EditorContext& context, Runs& runs, usize index)
        {
            Run& run = runs.active[index];
            IPieInstancePage& pie = *ServiceOf<IPieInstancePage>(run.page);
            const f64 t = pie.RunTime() - run.start;
            if (!pie.IsRunning())
            {
                return Finish(runs, index, u8"stopped", t);
            }

            if (run.shotInFlight)
            {
                const ViewportCapture& capture = pie.LastViewportCapture();
                if (capture.state == ViewportCaptureState::Written ||
                    capture.state == ViewportCaptureState::Failed)
                {
                    JsonValue shot = JsonValue::MakeObject();
                    shot.Set(u8"at", JsonValue::MakeNumber(run.shotAt));
                    if (capture.state == ViewportCaptureState::Written)
                    {
                        shot.Set(u8"path", JsonValue::MakeString(capture.path));
                        shot.Set(u8"width", JsonValue::MakeNumber(static_cast<f64>(capture.width)));
                        shot.Set(u8"height",
                                 JsonValue::MakeNumber(static_cast<f64>(capture.height)));
                    }
                    else
                    {
                        shot.Set(u8"error", JsonValue::MakeString(u8"the capture failed (log_read, "
                                                                  u8"category Screenshot, says "
                                                                  u8"why)"));
                    }
                    run.shots.Add(Move(shot));
                    run.shotInFlight = false;
                }
            }

            // Every sample time passed since the last pump is one row, read now.
            if (run.nextSample < run.sampleTimes.Size() && run.sampleTimes[run.nextSample] <= t)
            {
                while (run.nextSample < run.sampleTimes.Size() &&
                       run.sampleTimes[run.nextSample] <= t)
                {
                    ++run.nextSample;
                }
                TakeSample(pie, run, t);
            }

            if (run.hasUntil)
            {
                Result<JsonValue, String> value = ProbeValue(pie, run.until);
                if (value.HasValue() && Crosses(value.Value(), run.untilOp.AsView(), run.untilValue))
                {
                    JsonValue hit = JsonValue::MakeObject();
                    hit.Set(u8"probe", JsonValue::MakeString(run.until.label));
                    hit.Set(u8"op", JsonValue::MakeString(run.untilOp));
                    hit.Set(u8"value", Move(value.Value()));
                    hit.Set(u8"t", JsonValue::MakeNumber(t));
                    run.untilHit = Move(hit);
                    return Finish(runs, index, u8"until", t);
                }
            }

            if (!run.shotInFlight && run.nextShot < run.shotTimes.Size() &&
                run.shotTimes[run.nextShot] <= t)
            {
                run.shotAt = run.shotTimes[run.nextShot++];
                const String name = Format(u8"{}-{}-run{}-{}ms.png", pie.PieId(), ProcessId(),
                                           run.serial, static_cast<i64>(run.shotAt * 1000.0));
                context.RevealPage(run.page); // a hidden tab never renders
                pie.RequestViewportCapture(PathJoin(run.shotDirectory.AsView(), name.AsView()).AsView());
                run.shotInFlight = true;
            }

            if (t >= run.duration && !run.shotInFlight && run.nextShot >= run.shotTimes.Size())
            {
                return Finish(runs, index, u8"duration", t);
            }
            // The run clock stood still: the debugger holds the run, or the editor is not ticking.
            if (run.clock.Elapsed().AsSeconds() > run.duration * 4.0 + 30.0)
            {
                return Finish(runs, index, u8"timeout", t);
            }
            return ToolOutcome::NotFinished();
        }

        ToolOutcome Begin(EditorContext& context, Runs& runs, EditorPage* page,
                          const JsonValue& args)
        {
            IPieInstancePage& pie = *ServiceOf<IPieInstancePage>(page);
            if (!pie.IsRunning())
            {
                return Err(Format(u8"PIE instance '{}' is not running (pie_start runs it)",
                                  pie.PieId()));
            }
            Run run;
            const JsonValue durationArg = args.Get(u8"duration");
            if (!durationArg.IsNumber() || durationArg.AsNumber() <= 0.0 ||
                durationArg.AsNumber() > kMaxDuration)
            {
                return Err(Format(u8"`duration` takes run seconds, above 0 and at most {}",
                                  kMaxDuration));
            }
            run.duration = durationArg.AsNumber();

            auto source = MakeUnique<input::ScriptedInputSource>(context.Allocator(),
                                                                 context.Allocator());
            if (args.Has(u8"input"))
            {
                const JsonValue timeline = args.Get(u8"input");
                if (!timeline.IsArray())
                {
                    return Err(String(u8"`input` takes an array of timeline entries"));
                }
                for (i64 i = 0; i < timeline.Count(); ++i)
                {
                    Result<input::ScriptedInput, String> entry =
                        ParseInput(timeline.At(i), static_cast<usize>(i));
                    if (!entry.HasValue())
                    {
                        return Err(Move(entry.Error()));
                    }
                    if (entry.Value().at > run.duration)
                    {
                        return Err(Format(u8"input[{}]: `at` {} is after the run's end ({})", i,
                                          entry.Value().at, run.duration));
                    }
                    if (!source->Add(entry.Value()))
                    {
                        return Err(Format(u8"input[{}]: `gamepad` takes 0 to {}", i,
                                          input::ScriptedInputSource::kMaxGamepads - 1));
                    }
                }
            }
            run.inputCount = source->Count();

            // Probes, checked against the scene the game is in now: a typo fails here, not as a
            // column of nulls.
            String error;
            if (args.Has(u8"probes"))
            {
                const JsonValue probes = args.Get(u8"probes");
                if (!probes.IsArray())
                {
                    return Err(String(u8"`probes` takes an array"));
                }
                for (i64 i = 0; i < probes.Count(); ++i)
                {
                    if (!ParseProbes(probes.At(i), static_cast<usize>(i), run.probes, error))
                    {
                        return Err(Move(error));
                    }
                }
                for (const Probe& probe : run.probes)
                {
                    Result<JsonValue, String> value = ProbeValue(pie, probe);
                    if (!value.HasValue())
                    {
                        return Err(Move(value.Error()));
                    }
                }
            }
            if (!run.probes.IsEmpty() && !SampleTimes(args, run, error))
            {
                return Err(Move(error));
            }

            if (args.Has(u8"screenshots"))
            {
                const JsonValue shots = args.Get(u8"screenshots");
                if (!shots.IsArray())
                {
                    return Err(String(u8"`screenshots` takes an array of run times"));
                }
                for (i64 i = 0; i < shots.Count(); ++i)
                {
                    const JsonValue at = shots.At(i);
                    if (!at.IsNumber() || at.AsNumber() < 0.0 || at.AsNumber() > run.duration)
                    {
                        return Err(Format(u8"screenshots[{}]: a run time from 0 to the duration "
                                          u8"({})",
                                          i, run.duration));
                    }
                    run.shotTimes.PushBack(at.AsNumber());
                }
                SortTimes(run.shotTimes);
                if (!run.shotTimes.IsEmpty())
                {
                    run.shotDirectory = args.Get(u8"screenshotDir").AsString();
                    if (run.shotDirectory.IsEmpty())
                    {
                        run.shotDirectory = PathJoin(GetUserDataDirectory().AsView(),
                                                     u8"screenshots");
                        if (!CreateDirectories(run.shotDirectory.AsView()))
                        {
                            return Err(Format(u8"could not create '{}'",
                                              run.shotDirectory.AsView()));
                        }
                    }
                }
            }

            if (args.Has(u8"until"))
            {
                if (!ParseUntil(args.Get(u8"until"), run, error))
                {
                    return Err(Move(error));
                }
                Result<JsonValue, String> value = ProbeValue(pie, run.until);
                if (!value.HasValue())
                {
                    return Err(Move(value.Error()));
                }
            }

            run.page = page;
            run.start = pie.RunTime();
            run.startFrames = pie.FrameCount();
            run.serial = ++runs.serial;
            run.clock.Start();
            pie.BeginScriptedInput(Move(source));
            runs.active.PushBack(Move(run));
            return ToolOutcome::NotFinished();
        }

        ToolOutcome Handle(EditorContext& context, Runs& runs, const JsonValue& args)
        {
            // A run whose tab closed is over; its page pointer is never used again.
            for (usize i = runs.active.Size(); i > 0; --i)
            {
                if (!IsOpenPage(context, runs.active[i - 1].page))
                {
                    runs.active.RemoveAt(i - 1);
                }
            }
            Result<EditorPage*, String> resolved = ResolvePie(context, args);
            if (!resolved.HasValue())
            {
                return Err(Move(resolved.Error()));
            }
            for (usize i = 0; i < runs.active.Size(); ++i)
            {
                if (runs.active[i].page == resolved.Value())
                {
                    return Step(context, runs, i);
                }
            }
            return Begin(context, runs, resolved.Value(), args);
        }
    }

    void RegisterPieRunTool(foundation::mcp::McpServer& server, EditorContext& context)
    {
        JsonValue until = SchemaBuilder()
                              .Str(u8"entity", u8"the entity, by guid, name or slash path")
                              .Str(u8"field", u8"its field path, as in probes (default "
                                              u8"worldPosition; worldPosition.y for the height)")
                              .Str(u8"script", u8"or a game script property")
                              .Str(u8"op", u8"<, <=, >, >=, == or !=")
                              .Build();
        until.Set(u8"description",
                  JsonValue::MakeString(u8"end the run early when one value crosses: {entity, "
                                        u8"field, op, value} or {script, op, value}; `value` is a "
                                        u8"number, or with == and != a boolean or a string"));
        JsonValue schema =
            SchemaBuilder()
                .Str(u8"pie", u8"the PIE instance's id, as pie_list reports it (default: the "
                              u8"primary, `game-page`)")
                .Number(u8"duration", u8"run seconds to run, up to 600", true)
                .Arr(u8"input", u8"object",
                     u8"the timeline: entries {at, key, down} | {at, mouseButton, down} | {at, "
                     u8"mouseMove: [x, y]} | {at, wheel: [x, y]} | {at, gamepad, button, down} | "
                     u8"{at, gamepad, axis, value}; `at` is run seconds since the run starts, "
                     u8"`down` defaults to true, `gamepad` to 0")
                .Arr(u8"probes", u8"object",
                     u8"what to read: {entity, fields} (entity by guid, name or slash path; "
                     u8"fields default [\"worldPosition\"]) or {script: \"<game script "
                     u8"property>\"}")
                .Number(u8"every", u8"sample the probes every N run seconds (default 0.5)")
                .Arr(u8"sampleAt", u8"number", u8"or sample at these run times instead")
                .Arr(u8"screenshots", u8"number", u8"run times at which to write a PNG of the tab")
                .Str(u8"screenshotDir", u8"an existing directory for the screenshots (default: "
                                        u8"<user-data>/screenshots)")
                .Property(u8"until", Move(until))
                .Build();

        EditorContext* ctx = &context;
        auto runs = MakeUnique<Runs>(context.Allocator());
        Runs* runsPtr = runs.Get();
        server.RegisterTool(
            u8"pie_run",
            u8"Playtest one running PIE instance (pie_start first): play a device-level input "
            u8"timeline into it for `duration` run seconds and answer what happened. The timeline "
            u8"replaces that tab's real input for the run (the user's mouse and keys do not reach "
            u8"it; other instances keep theirs) and goes through the project's input map as a "
            u8"player's would: keys, mouse buttons, gamepad buttons and axes (-1 to 1) by their "
            u8"enums' case names, any case, as type_info lists them (KeyCode \"D\", \"Space\", "
            u8"\"LeftShift\"; MouseButton \"Left\"; GamepadButton \"South\"; GamepadAxis "
            u8"\"LeftX\"). Held keys are let go when the run ends. `probes` read entity fields "
            u8"(`worldPosition`, `position`, `rotation`, `scale`, `active`, or "
            u8"`<component>.<property>` as entity_inspect names them, then `.x`/`.y`/`.z` or a "
            u8"key or index to go inside) and game script properties, sampled `every` N seconds "
            u8"or at `sampleAt` times, one row per sample {t, frame, values} with a final row at "
            u8"the end; an entity missing at a sample reads null. `screenshots` writes the tab at "
            u8"those run times. `until` ends the run when its value crosses (\"Player "
            u8"worldPosition.y < -10\", \"script score >= 3\"). Returns {pie, endedBy: duration | "
            u8"until | stopped | timeout, runTime, frames, inputs, samples, screenshots, until, "
            u8"state}. Runs are real frames: a time lands within a frame of where it was asked, so "
            u8"compare with tolerances. Start from pie_start for a reproducible run. One run per "
            u8"instance at a time.",
            Move(schema), ToolAnnotations::Creates(),
            [ctx, runsPtr, keep = Move(runs)](const JsonValue& args) -> ToolOutcome
            { return Handle(*ctx, *runsPtr, args); });
    }
}
