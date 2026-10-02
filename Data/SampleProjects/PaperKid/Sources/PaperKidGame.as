// PaperKidGame - the run's orchestrator (the reserved class Game): the screens, the levels, the
// score and the lives.
//
// The Level script owns a block's rules and tells this class how it ended ("QuotaMet" with the
// whole seconds left, or "LevelFailed" with 0 for time, 1 for papers); every delivery
// ("Delivered", from a Subscriber) scores here. In a run the scene bus IS the run bus, so all of
// them reach this class.
//
// A run is the blocks in order. A cleared block banks its score (deliveries plus a time bonus); a
// failed one spends a life and replays from the banked score; no lives left, or the last block
// cleared, is the Game over screen. Pause (the Pause action) freezes the block under a menu;
// Settings, from the title or the pause menu, sets the audio buses' volumes, which the player saves
// when it exits, and goes back to whichever opened it (Documentation/Specs/paperkid.md).

Guid kTitleDoc = Guid("e04cdc8b-a360-464e-8127-e99cc0840817");
Guid kHudDoc = Guid("9b37c867-0090-4fde-b4de-80683c384002");
Guid kClearedDoc = Guid("11c81ce8-5a2a-4ef9-8f3b-b1071c319228");
Guid kFailedDoc = Guid("6f8c3e6a-8d67-4200-8847-30caa5854e2b");
Guid kGameOverDoc = Guid("b979eab4-4ff1-4df2-a245-4398a36230e2");
Guid kPauseDoc = Guid("de133f1d-cc35-4ed3-9827-56e660d9efad");
Guid kSettingsDoc = Guid("a77557c2-cd88-4fce-856c-30134997aef1");
Guid kStart = Guid("1a321fbc-7bfa-478e-a672-85351f51cba3");

const int kStartLives = 3;
const int kTimeBonusPerSecond = 5;

enum Phase
{
    Title,
    Playing,
    Paused,
    Settings,
    Ended
}

class Game
{
    private Phase m_phase = Phase::Title;
    private array<Guid> m_levels;
    private int m_level = 0;
    private int m_lives = kStartLives;
    private int m_score = 0;
    private int m_banked = 0; // the score when the current block started
    private int m_delivered = 0;
    private Phase m_settingsFrom = Phase::Title; // where Settings goes back to

    void launch()
    {
        m_levels.insertLast(Guid("4ca05200-9d61-4135-9fe5-cfc198ad8d44"));
        m_levels.insertLast(Guid("efa1e0c4-2619-4839-b1ee-54bd6b9561f7"));
        m_levels.insertLast(Guid("819a3ac3-580b-467e-8d96-464e6a1ab8f7"));
        m_levels.insertLast(Guid("933ec008-b426-4e2b-9075-b4d879ed58f1"));
        m_levels.insertLast(Guid("07ae3353-037a-44ba-9620-fe1d126e08a2"));
        // The default scene is the title's backdrop, frozen behind the menu.
        run::setTimeScale(0.0f);
        showTitle();
    }

    // The Game tier ticks at time scale 0 too, so the Pause action is read here in every phase.
    void update(float dt)
    {
        if (!Input::wasPressed("Pause"))
        {
            return;
        }
        if (m_phase == Phase::Playing)
        {
            pause();
        }
        else if (m_phase == Phase::Paused)
        {
            onResume();
        }
        else if (m_phase == Phase::Settings)
        {
            onSettingsBack();
        }
    }

    void exit() {}

    // ---- what the level reports ----

    void onDelivered(int points)
    {
        if (m_phase != Phase::Playing)
        {
            return;
        }
        m_delivered += 1;
        m_score += points;
        ui::findLabel("hud-score").setText("" + m_score);
    }

    void onQuotaMet(int secondsLeft)
    {
        if (m_phase != Phase::Playing)
        {
            return;
        }
        m_phase = Phase::Ended;
        run::setTimeScale(0.0f);
        int earned = m_score - m_banked;
        int bonus = secondsLeft * kTimeBonusPerSecond;
        m_score += bonus;
        m_banked = m_score;
        ui::findLabel("hud-score").setText("" + m_score);
        Screen@ s = ui::push(kClearedDoc);
        s.findLabel("cleared-summary").setText("" + m_delivered + " delivered, " + earned + " points + "
                                               + bonus + " time bonus");
        Button@ next = s.findButton("continue-btn");
        if (m_level + 1 >= int(m_levels.length()))
        {
            next.setText("Finish");
        }
        next.onClick(Action(this.onContinue));
    }

    void onLevelFailed(int reason)
    {
        if (m_phase != Phase::Playing)
        {
            return;
        }
        m_phase = Phase::Ended;
        run::setTimeScale(0.0f);
        m_lives -= 1;
        ui::findLabel("hud-lives").setText("" + m_lives);
        if (m_lives <= 0)
        {
            showGameOver(false);
            return;
        }
        Screen@ s = ui::push(kFailedDoc);
        s.findLabel("failed-reason").setText(reason == 0 ? "Out of time" : "Out of papers");
        s.findLabel("failed-lives").setText(m_lives == 1 ? "1 life left" : "" + m_lives + " lives left");
        s.findButton("retry-btn").onClick(Action(this.onRetry));
        s.findButton("menu-btn").onClick(Action(this.onToTitle));
    }

    // ---- buttons ----

    void onNewGame()
    {
        m_level = 0;
        m_lives = kStartLives;
        m_score = 0;
        m_banked = 0;
        startLevel();
    }

    void onRetry()
    {
        startLevel();
    }

    void onContinue()
    {
        m_level += 1;
        if (m_level >= int(m_levels.length()))
        {
            m_level = int(m_levels.length()) - 1;
            ui::pop(); // the Block cleared screen gives way to the summary
            showGameOver(true);
            return;
        }
        startLevel();
    }

    void onResume()
    {
        ui::pop(); // the pause menu
        run::setTimeScale(1.0f);
        m_phase = Phase::Playing;
    }

    void onSettings()
    {
        m_settingsFrom = m_phase;
        Screen@ s = ui::push(kSettingsDoc);
        bindVolume(s, "master", AudioBus::Master, Action(this.onMasterChanged));
        bindVolume(s, "music", AudioBus::Music, Action(this.onMusicChanged));
        bindVolume(s, "effects", AudioBus::Effects, Action(this.onEffectsChanged));
        s.findButton("settings-back-btn").onClick(Action(this.onSettingsBack));
        m_phase = Phase::Settings;
    }

    void onSettingsBack()
    {
        ui::pop(); // the settings screen; the title or the pause menu is under it
        m_phase = m_settingsFrom;
    }

    void onMasterChanged()
    {
        volumeChanged("master", AudioBus::Master);
    }

    void onMusicChanged()
    {
        volumeChanged("music", AudioBus::Music);
    }

    void onEffectsChanged()
    {
        volumeChanged("effects", AudioBus::Effects);
    }

    void onToTitle()
    {
        run::loadScene(kStart);
        run::setTimeScale(0.0f);
        showTitle();
    }

    void onQuit()
    {
        run::requestExit(0);
    }

    // ---- screens ----

    // The current block from its start: the score goes back to what was banked before it.
    private void startLevel()
    {
        m_score = m_banked;
        m_delivered = 0;
        ui::clear();
        ui::push(kHudDoc);
        ui::findLabel("hud-score").setText("" + m_score);
        ui::findLabel("hud-lives").setText("" + m_lives);
        run::setTimeScale(1.0f);
        run::loadScene(m_levels[m_level]);
        m_phase = Phase::Playing;
    }

    private void pause()
    {
        run::setTimeScale(0.0f);
        Screen@ s = ui::push(kPauseDoc);
        s.findButton("resume-btn").onClick(Action(this.onResume));
        s.findButton("restart-btn").onClick(Action(this.onRetry));
        s.findButton("settings-btn").onClick(Action(this.onSettings));
        s.findButton("menu-btn").onClick(Action(this.onToTitle));
        m_phase = Phase::Paused;
    }

    // A volume row: its slider at the bus's level, its readout, and the handler.
    private void bindVolume(Screen@ s, string row, AudioBus bus, Action@ handler)
    {
        Slider@ slider = s.findSlider(row + "-slider");
        slider.setValue(Audio::busVolume(bus));
        showVolume(row, slider.value);
        slider.onChanged(handler);
    }

    private void volumeChanged(string row, AudioBus bus)
    {
        float level = ui::findSlider(row + "-slider").value;
        Audio::setBusVolume(bus, level);
        showVolume(row, level);
    }

    private void showVolume(string row, float level)
    {
        ui::findLabel(row + "-value").setText("" + int(level * 100.0f + 0.5f) + "%");
    }

    private void showGameOver(bool won)
    {
        m_phase = Phase::Ended;
        Screen@ s = ui::push(kGameOverDoc);
        s.findLabel("over-title").setText(won ? "Route complete!" : "Game over");
        s.findLabel("over-score").setText("Score " + m_score);
        s.findLabel("over-reached").setText(won ? "Every block delivered"
                                                : "Reached block " + (m_level + 1) + " of " + m_levels.length());
        s.findButton("again-btn").onClick(Action(this.onNewGame));
        s.findButton("menu-btn").onClick(Action(this.onToTitle));
    }

    private void showTitle()
    {
        m_phase = Phase::Title;
        ui::clear();
        Screen@ s = ui::push(kTitleDoc);
        s.findButton("play-btn").onClick(Action(this.onNewGame));
        s.findButton("settings-btn").onClick(Action(this.onSettings));
        s.findButton("quit-btn").onClick(Action(this.onQuit));
    }
}
