// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene tests - the PIE tools (agent-playtesting-and-asset-creation.md P3) over a real
// EditorContext whose play actions open headless Game pages publishing IPieInstancePage: the
// start that answers once a frame has rendered, the primary already running, a new instance by
// its own id, the state with a faulted script's reason, the screenshot that waits for its frame,
// the stop of one or all, and the refusals.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.input;
import foundation.shell;
import foundation.scene;
import editor.core;
import editor.scene;

using namespace foundation::core;
using namespace foundation::mcp;
using namespace editor;
namespace json = foundation::json;
using json::JsonValue;

namespace
{
    // A Game tab with no viewport: its run is flags the test moves, as a frame would.
    class HeadlessGamePage final : public EditorPage, public IPieInstancePage
    {
    public:
        explicit HeadlessGamePage(StringView pieId)
            : EditorPage(DefaultAllocator()), m_pieId(pieId)
        {
            Provide<IPieInstancePage>(*this);
        }
        [[nodiscard]] StringView Title() const override { return u8"Game"; }
        [[nodiscard]] Status Save() override { return Status{}; }

        [[nodiscard]] StringView PieId() const noexcept override { return m_pieId.AsView(); }
        void Play() override
        {
            ++plays;
            starting = !failToStart;
        }
        void Stop() override
        {
            running = false;
            starting = false;
            scripted.Reset();
        }
        [[nodiscard]] bool IsRunning() const noexcept override { return running; }
        [[nodiscard]] bool IsStarting() const noexcept override { return starting; }
        [[nodiscard]] StringView SceneName() const noexcept override { return scene.AsView(); }
        [[nodiscard]] f64 RunTime() const noexcept override { return runTime; }
        [[nodiscard]] u64 FrameCount() const noexcept override { return frames; }
        [[nodiscard]] PieScriptState ScriptState() const noexcept override { return script; }
        [[nodiscard]] StringView ScriptFault() const noexcept override { return fault.AsView(); }
        void RequestViewportCapture(StringView path) override
        {
            capture = ViewportCapture{};
            capture.state = ViewportCaptureState::Pending;
            capture.path = String(path);
        }
        [[nodiscard]] const ViewportCapture& LastViewportCapture() const noexcept override
        {
            return capture;
        }

        // The run's scene, and a game script with one property, `score`.
        [[nodiscard]] foundation::scene::Scene* RunningScene() noexcept override
        {
            return running ? &level : nullptr;
        }
        [[nodiscard]] Result<Variant> GetScriptProperty(StringView name) const override
        {
            if (!running || name != u8"score")
            {
                return Err(ErrorCode::NotFound);
            }
            return Variant::From<f64>(score);
        }

        // The scripted input a run installed, advanced by Frame as the page's OnUpdate does.
        void BeginScriptedInput(UniquePtr<foundation::input::ScriptedInputSource> source) override
        {
            scripted = Move(source);
            scriptStart = runTime;
            scriptEnding = false;
            ++scriptsBegun;
        }
        void EndScriptedInput() override
        {
            scriptEnding = scripted.Get() != nullptr;
        }
        [[nodiscard]] bool IsScripted() const noexcept override { return scripted.Get() != nullptr; }

        /// One rendered frame of the run: the clock, then the page's advance of the script (an
        /// ended script goes, as the page's release frame then restore do).
        void Frame(f64 dt)
        {
            if (!running)
            {
                return;
            }
            ++frames;
            runTime += dt;
            if (scripted.Get() != nullptr)
            {
                if (scriptEnding)
                {
                    scripted.Reset();
                }
                else
                {
                    scripted->Advance(runTime - scriptStart);
                }
            }
        }

        /// The run's first frame: started, running, one frame drawn.
        void Run()
        {
            starting = false;
            running = true;
            frames = 1;
            runTime = 0.5;
        }

        bool running = false;
        bool starting = false;
        bool failToStart = false;
        u32 plays = 0;
        u64 frames = 0;
        f64 runTime = 0.0;
        String scene{u8"Arena"};
        foundation::scene::Scene level{DefaultAllocator(), u8"Level1"};
        f64 score = 0.0;
        UniquePtr<foundation::input::ScriptedInputSource> scripted;
        f64 scriptStart = 0.0;
        bool scriptEnding = false;
        u32 scriptsBegun = 0;
        PieScriptState script = PieScriptState::Running;
        String fault;
        ViewportCapture capture;

    private:
        String m_pieId;
    };

    // The editor's play actions, as the application declares them: the primary opens once,
    // a new instance opens another tab under the next id.
    struct PlayRig
    {
        EditorContext context{DefaultAllocator()};
        McpServer server;
        u32 extras = 0;
        HeadlessGamePage* primary = nullptr;

        PlayRig()
        {
            EditorActionDeclaration play;
            play.id = String(u8"game.play");
            play.label = String(u8"Play");
            play.execute = [this](EditorPage*)
            {
                if (primary == nullptr)
                {
                    primary = Open(u8"game-page");
                }
            };
            REQUIRE(context.Actions().Register(Move(play)));
            EditorActionDeclaration playNew;
            playNew.id = String(u8"game.playNewInstance");
            playNew.label = String(u8"Play New Instance");
            playNew.execute = [this](EditorPage*)
            { (void)Open(Format(u8"game-page-{}", ++extras).AsView()); };
            REQUIRE(context.Actions().Register(Move(playNew)));
            RegisterPieTools(server, context);
        }

        HeadlessGamePage* Open(StringView id)
        {
            return static_cast<HeadlessGamePage*>(context.AdoptPage(UniquePtr<EditorPage>(
                DefaultAllocator().New<HeadlessGamePage>(id), DefaultAllocator())));
        }

        HeadlessGamePage* Page(StringView id)
        {
            for (const UniquePtr<EditorPage>& open : context.OpenPages())
            {
                IPieInstancePage* pie = ServiceOf<IPieInstancePage>(open.Get());
                if (pie != nullptr && pie->PieId() == id)
                {
                    return static_cast<HeadlessGamePage*>(open.Get());
                }
            }
            return nullptr;
        }
    };

    struct Answer
    {
        bool finished = false;
        bool ok = false;
        JsonValue payload;
        String error;
    };

    Answer Pump(McpServer& server, StringView tool, StringView argumentsJson)
    {
        const String line = Format(u8"{{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\","
                                   u8"\"params\":{{\"name\":\"{}\",\"arguments\":{}}}}}",
                                   tool, argumentsJson);
        LineOutcome outcome = server.HandleLine(line.AsView());
        Answer answer;
        if (outcome.state != LineState::Answered)
        {
            return answer;
        }
        answer.finished = true;
        JsonValue result = json::Parse(outcome.response.AsView()).value.Get(u8"result");
        answer.ok = !result.Get(u8"isError").AsBool();
        const String text = result.Get(u8"content").At(0).Get(u8"text").AsString();
        if (answer.ok)
        {
            answer.payload = json::Parse(text.AsView()).value;
        }
        else
        {
            answer.error = text;
        }
        return answer;
    }
}

TEST_CASE("pie-tools: start waits for the first frame, a running primary answers at once, and a "
          "new instance has its own id")
{
    PlayRig rig;
    CHECK(rig.server.ToolCount() == kPieToolCount);

    // No tab yet: the state names how to get one.
    Answer got = Pump(rig.server, u8"pie_state", u8"{}");
    REQUIRE(got.finished);
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"pie_start"));

    // Start: the play action opens the tab, Play asks for the run; not finished until a frame.
    got = Pump(rig.server, u8"pie_start", u8"{}");
    CHECK_FALSE(got.finished);
    REQUIRE(rig.primary != nullptr);
    CHECK(rig.primary->plays == 1u);
    got = Pump(rig.server, u8"pie_start", u8"{}");
    CHECK_FALSE(got.finished); // still starting
    rig.primary->Run();
    got = Pump(rig.server, u8"pie_start", u8"{}");
    REQUIRE(got.finished);
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"pie").AsString() == StringView(u8"game-page"));
    CHECK(got.payload.Get(u8"running").AsBool());
    CHECK(got.payload.Get(u8"scene").AsString() == StringView(u8"Arena"));
    CHECK_FALSE(got.payload.Get(u8"alreadyRunning").AsBool());

    // Again: the running primary answers as it is.
    got = Pump(rig.server, u8"pie_start", u8"{}");
    REQUIRE(got.finished);
    CHECK(got.payload.Get(u8"alreadyRunning").AsBool());
    CHECK(rig.primary->plays == 1u);

    // A new instance: another tab, its own id.
    got = Pump(rig.server, u8"pie_start", u8"{\"newInstance\":true}");
    CHECK_FALSE(got.finished);
    HeadlessGamePage* second = rig.Page(u8"game-page-1");
    REQUIRE(second != nullptr);
    second->Run();
    got = Pump(rig.server, u8"pie_start", u8"{\"newInstance\":true}");
    REQUIRE(got.finished);
    CHECK(got.payload.Get(u8"pie").AsString() == StringView(u8"game-page-1"));

    // The list has both; the state of one by id carries a faulted script's reason.
    got = Pump(rig.server, u8"pie_list", u8"{}");
    CHECK(got.payload.Get(u8"count").AsNumber() == doctest::Approx(2.0));
    second->script = PieScriptState::Faulted;
    second->fault = String(u8"faulted in update: boom");
    got = Pump(rig.server, u8"pie_state", u8"{\"pie\":\"game-page-1\"}");
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"script").Get(u8"state").AsString() == StringView(u8"faulted"));
    CHECK(got.payload.Get(u8"script").Get(u8"fault").AsString() ==
          StringView(u8"faulted in update: boom"));
    CHECK(got.payload.Get(u8"runTime").AsNumber() == doctest::Approx(0.5));
    got = Pump(rig.server, u8"pie_state", u8"{\"pie\":\"game-page-9\"}");
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"pie_list"));

    // Stop one, then all: the ids stopped, the tabs stay.
    got = Pump(rig.server, u8"pie_stop", u8"{\"pie\":\"game-page-1\"}");
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"stopped").At(0).AsString() == StringView(u8"game-page-1"));
    CHECK_FALSE(second->running);
    got = Pump(rig.server, u8"pie_stop", u8"{\"all\":true}");
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"stopped").Count() == 1); // the primary; the other already stopped
    CHECK(rig.Page(u8"game-page-1") != nullptr);
}

TEST_CASE("pie-tools: a start that does not start is an error")
{
    PlayRig rig;
    rig.primary = rig.Open(u8"game-page");
    rig.primary->failToStart = true;
    Answer got = Pump(rig.server, u8"pie_start", u8"{}");
    CHECK_FALSE(got.finished);
    got = Pump(rig.server, u8"pie_start", u8"{}");
    REQUIRE(got.finished);
    CHECK_FALSE(got.ok);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"did not start"));
}

TEST_CASE("pie-tools: a screenshot waits for its frame, and a stopped instance is refused")
{
    PlayRig rig;
    rig.primary = rig.Open(u8"game-page");

    // Stopped: refused before anything is armed.
    Answer got = Pump(rig.server, u8"pie_screenshot", u8"{\"path\":\"pie.png\"}");
    REQUIRE(got.finished);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"not running"));

    rig.primary->Run();
    got = Pump(rig.server, u8"pie_screenshot", u8"{\"path\":\"pie.png\"}");
    CHECK_FALSE(got.finished);
    CHECK(rig.primary->capture.state == ViewportCaptureState::Pending);
    CHECK(rig.primary->capture.path == u8"pie.png");
    got = Pump(rig.server, u8"pie_screenshot", u8"{\"path\":\"pie.png\"}");
    CHECK_FALSE(got.finished); // not yet rendered

    // The frame lands: the PNG's size comes back.
    rig.primary->capture.state = ViewportCaptureState::Written;
    rig.primary->capture.width = 640;
    rig.primary->capture.height = 360;
    got = Pump(rig.server, u8"pie_screenshot", u8"{\"path\":\"pie.png\"}");
    REQUIRE(got.finished);
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"pie").AsString() == StringView(u8"game-page"));
    CHECK(got.payload.Get(u8"width").AsNumber() == doctest::Approx(640.0));

    // A run that stops mid-capture is the capture's failure.
    got = Pump(rig.server, u8"pie_screenshot", u8"{\"path\":\"pie2.png\"}");
    CHECK_FALSE(got.finished);
    rig.primary->Stop();
    got = Pump(rig.server, u8"pie_screenshot", u8"{\"path\":\"pie2.png\"}");
    REQUIRE(got.finished);
    CHECK(got.error.AsView().ContainsIgnoreCase(u8"stopped before"));
}

// Sedulous aaf5ff78: the HTTP host re-enters every unfinished call each pump, so captures of two
// instances interleave; each waits for its own frame.
TEST_CASE("pie-tools: screenshots of two instances wait side by side")
{
    PlayRig rig;
    rig.primary = rig.Open(u8"game-page");
    HeadlessGamePage* client = rig.Open(u8"game-page-1");
    rig.primary->Run();
    client->Run();
    const StringView hostArgs = u8"{\"pie\":\"game-page\",\"path\":\"host.png\"}";
    const StringView clientArgs = u8"{\"pie\":\"game-page-1\",\"path\":\"client.png\"}";
    CHECK_FALSE(Pump(rig.server, u8"pie_screenshot", hostArgs).finished);
    CHECK_FALSE(Pump(rig.server, u8"pie_screenshot", clientArgs).finished);
    CHECK(client->capture.path == u8"client.png");
    CHECK(rig.primary->capture.path == u8"host.png"); // the second did not re-arm the first

    client->capture.state = ViewportCaptureState::Written;
    CHECK_FALSE(Pump(rig.server, u8"pie_screenshot", hostArgs).finished);
    Answer got = Pump(rig.server, u8"pie_screenshot", clientArgs);
    REQUIRE(got.finished);
    CHECK(got.payload.Get(u8"pie").AsString() == StringView(u8"game-page-1"));
    rig.primary->capture.state = ViewportCaptureState::Written;
    got = Pump(rig.server, u8"pie_screenshot", hostArgs);
    REQUIRE(got.finished);
    CHECK(got.payload.Get(u8"path").AsString() == StringView(u8"host.png"));
}

TEST_CASE("pie-tools: entity_inspect reads a running game's entity by `pie`")
{
    PlayRig rig;
    RegisterSceneLiveTools(rig.server, rig.context);
    rig.primary = rig.Open(u8"game-page");
    rig.primary->Run();
    const foundation::scene::EntityHandle player = rig.primary->level.CreateEntity(u8"Player");
    Transform placed;
    placed.position = Float3{4.0f, 0.0f, 0.0f};
    rig.primary->level.SetLocalTransform(player, placed);

    Answer got = Pump(rig.server, u8"entity_inspect", u8"{\"pie\":\"game-page\",\"entity\":\"Player\"}");
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"pie").AsString() == StringView(u8"game-page"));
    CHECK(got.payload.Get(u8"scene").AsString() == StringView(u8"Level1"));
    CHECK(got.payload.Get(u8"entity").Get(u8"name").AsString() == StringView(u8"Player"));
    CHECK(got.payload.Get(u8"entity").Get(u8"transform").Get(u8"position").At(0).AsNumber() ==
          doctest::Approx(4.0));

    got = Pump(rig.server, u8"entity_inspect", u8"{\"pie\":\"game-page\",\"entity\":\"Ghost\"}");
    CHECK(got.error.AsView().StartsWith(u8"no entity 'Ghost' in PIE instance 'game-page''s scene 'Level1'"));
    got = Pump(rig.server, u8"entity_inspect", u8"{\"pie\":\"game-page\"}");
    CHECK(got.error.AsView().StartsWith(u8"pass `entity`"));
    rig.primary->Stop();
    got = Pump(rig.server, u8"entity_inspect", u8"{\"pie\":\"game-page\",\"entity\":\"Player\"}");
    CHECK(got.error.AsView().StartsWith(u8"PIE instance 'game-page' is not running a scene"));
}

// ---- pie_run (agent-playtesting-and-asset-creation.md P4, Sedulous aaf5ff78) ----

namespace
{
    /// A stand-in game frame: the page's frame (its clock and its script), then the "game": the
    /// player walks +x at one unit a second while D is held, and a requested capture lands.
    void PlayFrame(HeadlessGamePage& page, f64 dt)
    {
        page.Frame(dt);
        if (!page.running)
        {
            return;
        }
        const foundation::scene::EntityHandle player = page.level.FindEntityByName(u8"Player");
        if (page.scripted.Get() != nullptr &&
            page.scripted->Keyboard()->IsKeyDown(foundation::shell::KeyCode::D) &&
            page.level.IsValid(player))
        {
            Transform t = page.level.GetLocalTransform(player);
            t.position.x += static_cast<f32>(dt);
            page.level.SetLocalTransform(player, t);
        }
        if (page.capture.state == ViewportCaptureState::Pending)
        {
            page.capture.state = ViewportCaptureState::Written;
            page.capture.width = 640;
            page.capture.height = 360;
        }
    }

    HeadlessGamePage* RunningPie(PlayRig& rig, StringView id)
    {
        HeadlessGamePage* page = rig.Open(id);
        page->running = true;
        page->scene = String(u8"Level1");
        (void)page->level.CreateEntity(u8"Player");
        return page;
    }

    /// Pumps one call until it answers, a frame of the page between pumps.
    Answer RunToEnd(PlayRig& rig, StringView args, HeadlessGamePage& page, f64 dt)
    {
        for (u32 pumps = 0; pumps < 1000; ++pumps)
        {
            Answer answer = Pump(rig.server, u8"pie_run", args);
            if (answer.finished)
            {
                return answer;
            }
            PlayFrame(page, dt);
        }
        FAIL("the run never ended");
        return Answer{};
    }
}

TEST_CASE("pie-tools: a run plays its timeline, samples, shoots, and ends")
{
    PlayRig rig;
    HeadlessGamePage* host = RunningPie(rig, u8"game-page");

    // Refusals change nothing: no script is installed.
    Answer got = Pump(rig.server, u8"pie_run", u8"{\"duration\":1,\"input\":[{\"at\":0,\"key\":\"Hyper\"}]}");
    CHECK(got.error.AsView().StartsWith(u8"input[0]: no key 'Hyper'"));
    got = Pump(rig.server, u8"pie_run", u8"{\"duration\":1,\"input\":[{\"at\":2,\"key\":\"D\"}]}");
    CHECK(got.error.AsView().StartsWith(u8"input[0]: `at` 2 is after the run's end"));
    got = Pump(rig.server, u8"pie_run", u8"{\"duration\":1,\"input\":[{\"at\":0,\"gamepad\":4,\"button\":\"South\"}]}");
    CHECK(got.error.AsView().StartsWith(u8"input[0]: `gamepad` takes 0 to 3"));
    got = Pump(rig.server, u8"pie_run", u8"{\"duration\":1,\"probes\":[{\"entity\":\"Player\",\"fields\":[\"light.intensity\"]}]}");
    CHECK(got.error.AsView().StartsWith(u8"entity 'Player' has no field 'light.intensity'"));
    got = Pump(rig.server, u8"pie_run", u8"{\"duration\":1,\"probes\":[{\"entity\":\"Ghost\"}]}");
    CHECK(got.error.AsView().StartsWith(u8"no entity 'Ghost' in PIE instance 'game-page''s scene 'Level1'"));
    got = Pump(rig.server, u8"pie_run", u8"{\"duration\":1,\"probes\":[{\"script\":\"lives\"}]}");
    CHECK(got.error.AsView().StartsWith(u8"the game script of PIE instance 'game-page' has no property 'lives'"));
    got = Pump(rig.server, u8"pie_run", u8"{\"duration\":0}");
    CHECK(got.error.AsView().StartsWith(u8"`duration` takes run seconds"));
    CHECK(host->scriptsBegun == 0u);

    // Hold D for a second of a two second run, sampling the player and the score every half
    // second, with a screenshot at one second.
    const StringView args =
        u8"{\"duration\":2,\"input\":[{\"at\":0,\"key\":\"D\"},{\"at\":1,\"key\":\"d\",\"down\":false}],"
        u8"\"probes\":[{\"entity\":\"Player\",\"fields\":[\"position.x\"]},{\"script\":\"score\"}],"
        u8"\"every\":0.5,\"screenshots\":[1],\"screenshotDir\":\"shots\"}";
    got = Pump(rig.server, u8"pie_run", args);
    CHECK_FALSE(got.finished);
    CHECK(host->IsScripted());
    CHECK(host->scriptsBegun == 1u);
    host->score = 7.0;
    got = RunToEnd(rig, args, *host, 0.05);
    REQUIRE(got.ok);
    const JsonValue& result = got.payload;
    CHECK(result.Get(u8"endedBy").AsString() == StringView(u8"duration"));
    CHECK(result.Get(u8"runTime").AsNumber() >= 2.0 - 1e-9);
    CHECK(result.Get(u8"inputs").AsNumber() == doctest::Approx(2.0));
    const JsonValue samples = result.Get(u8"samples");
    // 0, 0.5, 1, 1.5, 2, each read on the frame that reached it; no extra final row.
    REQUIRE(samples.Count() == 5);
    f64 last = -1.0;
    for (i64 i = 0; i < samples.Count(); ++i)
    {
        const f64 t = samples.At(i).Get(u8"t").AsNumber();
        CHECK(t > last);
        CHECK(t >= static_cast<f64>(i) * 0.5 - 1e-9); // a row is read once its time is reached
        last = t;
    }
    // Walked while D was held (a second, give or take a frame), then stood.
    const f64 x = samples.At(4).Get(u8"values").Get(u8"Player.position.x").AsNumber();
    CHECK(x == doctest::Approx(1.0).epsilon(0.1));
    CHECK(samples.At(4).Get(u8"values").Get(u8"script.score").AsNumber() == doctest::Approx(7.0));
    const JsonValue shots = result.Get(u8"screenshots");
    REQUIRE(shots.Count() == 1);
    CHECK(shots.At(0).Get(u8"at").AsNumber() == doctest::Approx(1.0));
    CHECK(shots.At(0).Get(u8"path").AsString().AsView().StartsWith(u8"shots/game-page-"));
    CHECK(shots.At(0).Get(u8"width").AsNumber() == doctest::Approx(640.0));
    CHECK(result.Get(u8"until").IsNull());
    CHECK(result.Get(u8"state").Get(u8"running").AsBool());

    // The script ends: its release, then the viewport's input back.
    CHECK(host->scriptEnding);
    PlayFrame(*host, 0.05);
    CHECK_FALSE(host->IsScripted());
}

TEST_CASE("pie-tools: until ends a run, and a stop ends it too")
{
    PlayRig rig;
    HeadlessGamePage* host = RunningPie(rig, u8"game-page");

    const StringView args = u8"{\"duration\":10,\"input\":[{\"at\":0,\"key\":\"d\"}],\"until\":"
                            u8"{\"entity\":\"Player\",\"field\":\"position.x\",\"op\":\">=\",\"value\":1.5}}";
    Answer got = RunToEnd(rig, args, *host, 0.1);
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"endedBy").AsString() == StringView(u8"until"));
    const JsonValue hit = got.payload.Get(u8"until");
    CHECK(hit.Get(u8"probe").AsString() == StringView(u8"Player.position.x"));
    CHECK(hit.Get(u8"value").AsNumber() >= 1.5);
    CHECK(got.payload.Get(u8"runTime").AsNumber() < 2.0);
    CHECK(got.payload.Get(u8"samples").Count() == 0); // no probes, no rows

    got = Pump(rig.server, u8"pie_run", u8"{\"duration\":1,\"until\":{\"script\":\"score\",\"op\":\"~\",\"value\":1}}");
    CHECK(got.error.AsView().StartsWith(u8"`until.op` takes"));
    got = Pump(rig.server, u8"pie_run", u8"{\"duration\":1,\"until\":{\"script\":\"score\",\"op\":\"<\",\"value\":\"a\"}}");
    CHECK(got.error.AsView().StartsWith(u8"`until` compares a boolean or a string with == or != only"));

    // A stop mid-run answers what it had, and says so.
    PlayFrame(*host, 0.1); // the ended script goes
    const StringView longRun = u8"{\"duration\":30,\"input\":[{\"at\":0,\"key\":\"D\"}],"
                               u8"\"probes\":[{\"entity\":\"Player\",\"fields\":[\"position.x\"]}]}";
    CHECK_FALSE(Pump(rig.server, u8"pie_run", longRun).finished);
    PlayFrame(*host, 0.1);
    CHECK_FALSE(Pump(rig.server, u8"pie_run", longRun).finished);
    host->Stop();
    got = Pump(rig.server, u8"pie_run", longRun);
    REQUIRE(got.finished);
    REQUIRE(got.ok);
    CHECK(got.payload.Get(u8"endedBy").AsString() == StringView(u8"stopped"));
    CHECK(got.payload.Get(u8"samples").Count() == 1);
    CHECK_FALSE(got.payload.Get(u8"state").Get(u8"running").AsBool());
}

TEST_CASE("pie-tools: two instances run side by side, and only the scripted one moves")
{
    PlayRig rig;
    HeadlessGamePage* host = RunningPie(rig, u8"game-page");
    HeadlessGamePage* client = RunningPie(rig, u8"game-page-1");

    // The host walks; the client runs a timeline with nothing in it, over the same seconds.
    const StringView hostArgs = u8"{\"pie\":\"game-page\",\"duration\":1,\"input\":[{\"at\":0,\"key\":\"D\"}],"
                                u8"\"probes\":[{\"entity\":\"Player\",\"fields\":[\"position.x\"]}]}";
    const StringView clientArgs = u8"{\"pie\":\"game-page-1\",\"duration\":1,"
                                  u8"\"probes\":[{\"entity\":\"Player\",\"fields\":[\"position.x\"]}]}";
    Answer hostAnswer;
    Answer clientAnswer;
    for (u32 pumps = 0; pumps < 100 && (!hostAnswer.finished || !clientAnswer.finished); ++pumps)
    {
        if (!hostAnswer.finished)
        {
            hostAnswer = Pump(rig.server, u8"pie_run", hostArgs);
        }
        if (!clientAnswer.finished)
        {
            clientAnswer = Pump(rig.server, u8"pie_run", clientArgs);
        }
        PlayFrame(*host, 0.1);
        PlayFrame(*client, 0.1);
    }
    REQUIRE(hostAnswer.ok);
    REQUIRE(clientAnswer.ok);
    CHECK(hostAnswer.payload.Get(u8"pie").AsString() == StringView(u8"game-page"));
    CHECK(clientAnswer.payload.Get(u8"pie").AsString() == StringView(u8"game-page-1"));
    const JsonValue hostSamples = hostAnswer.payload.Get(u8"samples");
    const JsonValue clientSamples = clientAnswer.payload.Get(u8"samples");
    CHECK(hostSamples.At(hostSamples.Count() - 1).Get(u8"values").Get(u8"Player.position.x").AsNumber() > 0.8);
    CHECK(clientSamples.At(clientSamples.Count() - 1).Get(u8"values").Get(u8"Player.position.x").AsNumber() ==
          doctest::Approx(0.0));
}
