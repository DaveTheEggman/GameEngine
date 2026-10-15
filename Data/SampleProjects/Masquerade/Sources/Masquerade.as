// Masquerade - the game's orchestrator (the reserved class Game): the title, a round at the ball
// under its HUD, the pause menu, and the results.
//
// The round itself is the scene's (Ball.as): this hears it. "Hud" (four times a second: your
// points, the seconds left, your target's costume, your place), "Note" (something happened to
// you, or a mask flared) and "RoundOver" (everyone's points). Pause (Escape or Start) holds the
// round still under the pause menu. The title picks the map; the save keeps each map's best round
// ("best.Palace", "best.Canal", "best.Gardens").

const float kControlsShown = 7.0f; // how long the controls show as a round starts (s)
Guid kTitleDoc = Guid("aef79ca5-c3b5-4a90-b02f-ba8f04a22bd5");
Guid kHudDoc = Guid("2c758b72-4a6a-49d9-a13b-c2646c166f04");
Guid kPauseDoc = Guid("a8e7955f-11cb-4e14-a8dc-d05e2a923722");
Guid kResultsDoc = Guid("3d968c6d-1b40-4e01-8f82-d36fd8008188");
Guid kPalace = Guid("20eb69af-217d-4b37-93d2-d032e63094fb");
Guid kCanal = Guid("67c261b3-8da6-4a11-bab2-c96595b651cf");
Guid kGardens = Guid("6c8abc4e-60ff-4614-864b-68fd5dc21e19");

// Ball.as's notes.
const int kNoteGotTarget = 1;
const int kNoteMissed = 2;
const int kNoteTagged = 3;
const int kNoteFlare = 4;

class Game
{
    private bool m_inRound = false;
    private bool m_paused = false;
    private float m_note = 0.0f;         // how long the HUD's word shows yet
    private float m_controlsShow = 0.0f; // how long the controls panel shows yet
    private int m_map = 0;               // 0 the Palace, 1 the Canal, 2 the Gardens

    void launch()
    {
        showTitle();
    }

    // ---- the title ----
    private void showTitle()
    {
        m_inRound = false;
        m_paused = false;
        run::setTimeScale(0.0f); // whatever is behind the title holds still
        ui::clear();
        Screen@ s = ui::push(kTitleDoc);
        s.findButton("play-btn").onClick(Action(this.onPalace));
        s.findButton("canal-btn").onClick(Action(this.onCanal));
        s.findButton("canal-btn").setEnabled(!kCanal.IsNil());
        s.findButton("gardens-btn").onClick(Action(this.onGardens));
        s.findButton("gardens-btn").setEnabled(!kGardens.IsNil());
        s.findButton("quit-btn").onClick(Action(this.onQuit));
        s.findLabel("palace-best").setText(bestLine(0));
        s.findLabel("canal-best").setText(bestLine(1));
        s.findLabel("gardens-best").setText(bestLine(2));
    }

    private void onPalace() { m_map = 0; startRound(); }
    private void onCanal() { m_map = 1; startRound(); }
    private void onGardens() { m_map = 2; startRound(); }

    private string mapName(int map) { return map == 0 ? "Palace" : (map == 1 ? "Canal" : "Gardens"); }

    private string bestLine(int map)
    {
        int best = Save::getInt("best." + mapName(map), -100000);
        return best == -100000 ? "No round played yet" : "Best round: " + best + " points";
    }
    private void onQuit() { run::requestExit(0); }

    // ---- a round ----
    private void startRound()
    {
        m_paused = false;
        m_inRound = true;
        m_note = 0.0f;
        ui::clear();
        ui::push(kHudDoc);
        ui::findLabel("hud-target").setText("");
        ui::findLabel("hud-score").setText("Points 0");
        View@ controls = ui::find("hud-controls");
        controls.setVisible(true);
        controls.setOpacity(0.0f);
        controls.fadeTo(1.0f, 0.3f);
        m_controlsShow = kControlsShown;
        run::setTimeScale(1.0f);
        run::loadScene(m_map == 0 ? kPalace : (m_map == 1 ? kCanal : kGardens));
    }

    void update(float dt)
    {
        if (!m_inRound)
        {
            return;
        }
        if (Input::wasPressed("Pause"))
        {
            if (m_paused)
            {
                onResume();
            }
            else
            {
                pause();
            }
            return;
        }
        if (m_paused)
        {
            return;
        }
        if (m_controlsShow > 0.0f)
        {
            m_controlsShow -= dt;
            if (m_controlsShow <= 0.0f)
            {
                ui::find("hud-controls").fadeTo(0.0f, 0.6f);
            }
        }
        if (m_note > 0.0f)
        {
            m_note -= dt;
            if (m_note <= 0.0f)
            {
                ui::find("hud-note").fadeTo(0.0f, 0.5f);
            }
        }
    }

    // ---- what the round reports ----
    void onHud(Float4 hud)
    {
        if (!m_inRound)
        {
            return;
        }
        ui::findLabel("hud-target").setText("Your mark wears " + costumeName(int(hud.z)));
        ui::findLabel("hud-score").setText("Points " + int(hud.x));
        ui::findLabel("hud-time").setText(clock(hud.y));
        ui::findLabel("hud-rank").setText(place(int(hud.w)) + " of 4");
    }

    void onNote(int what)
    {
        if (!m_inRound)
        {
            return;
        }
        if (what == kNoteGotTarget)
        {
            note("Your mark is tagged. They will be back, dressed anew");
        }
        else if (what == kNoteMissed)
        {
            note("Wrong guest! Your mask flares");
        }
        else if (what == kNoteTagged)
        {
            note("Tagged! A new costume, somewhere else");
        }
        else if (what == kNoteFlare)
        {
            note("A mask flares: a hunter, there");
        }
    }

    void onRoundOver(Float4 scores)
    {
        if (!m_inRound)
        {
            return;
        }
        m_inRound = false;
        run::setTimeScale(0.0f);
        int you = int(scores.x);
        string key = "best." + mapName(m_map);
        int best = Save::getInt(key, -100000);
        bool record = you > best;
        if (record)
        {
            Save::setInt(key, you);
            Save::flush();
        }
        int place = 1;
        if (scores.y > scores.x) ++place;
        if (scores.z > scores.x) ++place;
        if (scores.w > scores.x) ++place;
        Screen@ s = ui::push(kResultsDoc);
        s.findLabel("res-title").setText(place == 1 ? "The ball is yours" : "The music stops");
        s.findLabel("res-you").setText("You came " + this.place(place) + " with " + you + " points");
        s.findLabel("res-0").setText("You        " + you);
        s.findLabel("res-1").setText("Hunter 1   " + int(scores.y));
        s.findLabel("res-2").setText("Hunter 2   " + int(scores.z));
        s.findLabel("res-3").setText("Hunter 3   " + int(scores.w));
        s.findLabel("res-best").setText(record ? "Your best round yet" : "Your best: " + best + " points");
        s.findButton("again-btn").onClick(Action(this.onAgain));
        s.findButton("title-btn").onClick(Action(this.onTitle));
    }

    // ---- the pause menu ----
    private void pause()
    {
        m_paused = true;
        run::setTimeScale(0.0f);
        m_controlsShow = 0.0f;
        ui::find("hud-controls").setVisible(false); // the menu shows them itself
        Screen@ s = ui::push(kPauseDoc);
        s.findButton("resume-btn").onClick(Action(this.onResume));
        s.findButton("restart-btn").onClick(Action(this.onAgain));
        s.findButton("title-btn").onClick(Action(this.onTitle));
    }

    private void onResume()
    {
        if (!m_paused)
        {
            return;
        }
        m_paused = false;
        ui::pop();
        run::setTimeScale(1.0f);
    }

    private void onAgain() { startRound(); }
    private void onTitle() { showTitle(); }

    private void note(string text)
    {
        Label@ label = ui::findLabel("hud-note");
        label.setText(text);
        label.setOpacity(1.0f);
        label.setVisible(true);
        m_note = 3.0f;
    }

    // In blender/guest.py's costume order, which the crowd and the looks follow.
    private string costumeName(int costume)
    {
        if (costume == 0) return "a rose gown";
        if (costume == 1) return "a teal gown";
        if (costume == 2) return "a gold gown";
        if (costume == 3) return "a black coat";
        if (costume == 4) return "a plum coat";
        return "an ivory coat";
    }

    private string place(int p)
    {
        return p == 1 ? "1st" : (p == 2 ? "2nd" : (p == 3 ? "3rd" : "4th"));
    }

    private string clock(float seconds)
    {
        int s = int(seconds);
        int m = s / 60;
        s = s % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }
}
