// PaperKidGame - the Game-tier orchestrator (P0: the skeleton + screen flow).
//
// This is the project's startup script (Project.xml startupScriptId), run by the play-in-editor
// GameEditorPage as the reserved `Game` tier: launch() boots it, update(dt) ticks it, exit() tears
// it down, and on<Event>(...) methods harvest the run bus. It drives the whole screen/state flow
// over the run + ui facades for the P1 driving slice (bike, deliveries, scoring, level flow).
//
// Screens are authored UIDocuments (Sources/screens/*.sml), pushed by guid via ui::push; their
// id-tagged buttons are wired to handlers with button.onClick(Action(this.handler)). The empty
// "Playing" level is a scene loaded with run::loadScene.
//
// Asset guids are referenced as Guid("...") - the canonical UUID string, copied straight from the
// matching .xasset envelope. These MUST match the envelopes; a malformed string parses to Nil.

// --- authored asset guids (keep in sync with the .xasset envelopes) ---
Guid kMainMenuDoc = Guid("ac96b003-5b7c-433f-896c-489befb6e2c2");     // main-menu
Guid kPauseDoc = Guid("985eb393-4110-4fc4-9742-7bac60ca136d");        // pause
Guid kSettingsDoc = Guid("d30677d4-cb28-4ec1-958a-f8046e0c67a5");     // settings
Guid kPlayingLevel = Guid("855ffed4-4da7-4fa0-9756-a95c6c842890");    // MainScene
Guid kLevelClearedDoc = Guid("1df972ca-bcdd-4a73-bda6-4df4a858249f"); // level-cleared
Guid kLevelFailedDoc = Guid("3aa68e31-9f1c-44e6-8cd6-48f2f7e64d42");  // level-failed
Guid kHudDoc = Guid("99b0b28b-bca7-4b49-b0df-ed034b176973");          // hud (overlay)

enum GameState
{
    Booting,
    MainMenu,
    Playing,
    Paused,
    Settings,
    LevelCleared,
    LevelFailed
}

class Game
{
    GameState m_state = GameState::Booting;
    // Where "Back" returns from the Settings screen (Settings opens from both Main menu and Pause).
    GameState m_settingsReturn = GameState::MainMenu;

    // ---- scoring (P1-6) ----
    int m_score = 0;
    int m_deliveries = 0;

    // ---- Game-tier lifecycle ----
    void launch()
    {
        // Boot straight into the main menu. No scene is loaded yet (defaultSceneId is empty); the
        // Game owns level loading via run::loadScene when the player hits Start.
        showMainMenu();
    }

    void update(float dt)
    {
        // ESC toggles pause. Reads a "Pause" button action (bind it to ESC in the input map);
        // Input is action-based, so the action must exist + be in the active set to fire.
        if (Input::wasPressed("Pause"))
        {
            if (m_state == GameState::Playing)
            {
                showPause();
            }
            else if (m_state == GameState::Paused)
            {
                onResume();
            }
        }
        // Later: drive the countdown timer while Playing, etc.
    }

    void exit()
    {
        ui::clear();
    }

    // ---- screens: each clears the stack and pushes one document, then wires its buttons ----
    void showMainMenu()
    {
        run::setTimeScale(0.0f); // no gameplay behind a menu (freezes any loaded scene)
        ui::clear();
        Screen menu = ui::push(kMainMenuDoc);
        menu.findButton("start-btn").onClick(Action(this.onStartGame));
        menu.findButton("settings-btn").onClick(Action(this.onOpenSettingsFromMenu));
        menu.findButton("quit-btn").onClick(Action(this.onQuitGame));
        m_state = GameState::MainMenu;
    }

    void enterPlaying()
    {
        // Fresh score, then drop the menus and put up the overlay HUD BEFORE the scene starts, so the
        // Level/Bike onStart handlers have its labels to populate. The HUD is an Overlay screen: it
        // passes input through to the bike (a Modal screen would freeze gameplay).
        m_score = 0;
        m_deliveries = 0;
        ui::clear();
        ui::push(kHudDoc);
        run::loadScene(kPlayingLevel);
        run::setTimeScale(1.0f); // gameplay runs
        m_state = GameState::Playing;
    }

    void showPause()
    {
        // Pause OVERLAYS the running scene (modal); timescale 0 actually freezes gameplay under it.
        run::setTimeScale(0.0f);
        Screen pause = ui::push(kPauseDoc);
        pause.findButton("resume-btn").onClick(Action(this.onResume));
        pause.findButton("settings-btn").onClick(Action(this.onOpenSettingsFromPause));
        pause.findButton("quit-btn").onClick(Action(this.onQuitToMenu));
        m_state = GameState::Paused;
    }

    void showSettings()
    {
        Screen settings = ui::push(kSettingsDoc);
        settings.findButton("back-btn").onClick(Action(this.onSettingsBack));
        m_state = GameState::Settings;
    }

    // ---- button handlers (deferred + mutation-queue-safe via the onClick seam) ----
    void onStartGame() { enterPlaying(); }
    void onQuitGame() { run::requestExit(); } // ends the run (editor: stops the play session)

    void onResume()
    {
        ui::pop(); // drop the pause overlay, back to Playing
        run::setTimeScale(1.0f); // gameplay resumes
        m_state = GameState::Playing;
    }
    void onQuitToMenu() { showMainMenu(); }

    void onOpenSettingsFromMenu()
    {
        m_settingsReturn = GameState::MainMenu;
        showSettings();
    }
    void onOpenSettingsFromPause()
    {
        m_settingsReturn = GameState::Paused;
        showSettings();
    }
    void onSettingsBack()
    {
        // Return to whichever screen opened Settings.
        if (m_settingsReturn == GameState::Paused)
        {
            ui::pop(); // settings was pushed OVER the pause overlay - just drop it
            m_state = GameState::Paused;
        }
        else
        {
            showMainMenu();
        }
    }

    // ---- run-bus inbox (P1-6): scene bus == run bus, so these arrive directly; the Game scores + ends the level ----
    // A delivery landed (Subscriber's "Delivered", carrying the house's points).
    void onDelivered(int points)
    {
        if (m_state != GameState::Playing)
        {
            return;
        }
        m_deliveries += 1;
        m_score += points;
        Log::info("Delivered"); // deliveries/score tracked in m_deliveries/m_score (HUD in P1-7)
    }

    // The delivery quota was met -> level cleared.
    void onQuotaMet(int deliveries)
    {
        if (m_state != GameState::Playing)
        {
            return;
        }
        showLevelCleared();
    }

    // The level failed. The Level tells us WHY: reason 0 = the timer ran out, reason 1 = out of
    // papers (with quota unmet after the in-flight grace). The screen title reflects the reason.
    void onLevelFailed(int reason)
    {
        if (m_state != GameState::Playing)
        {
            return;
        }
        showLevelFailed(reason);
    }

    // ---- end-of-level screens (P1-7): a modal over the frozen scene + a live score summary ----
    void showLevelCleared()
    {
        run::setTimeScale(0.0f); // freeze the scene under the results modal
        Screen s = ui::push(kLevelClearedDoc);
        s.findLabel("summary").setText("Delivered " + m_deliveries + " papers    Score " + m_score);
        s.findButton("play-again-btn").onClick(Action(this.onPlayAgain));
        s.findButton("menu-btn").onClick(Action(this.onBackToMenu));
        m_state = GameState::LevelCleared;
    }

    void showLevelFailed(int reason)
    {
        run::setTimeScale(0.0f);
        Screen s = ui::push(kLevelFailedDoc);
        s.findLabel("title").setText(reason == 1 ? "Out of Papers!" : "Time's Up!");
        s.findLabel("summary").setText("Delivered " + m_deliveries + " papers    Score " + m_score);
        s.findButton("retry-btn").onClick(Action(this.onPlayAgain));
        s.findButton("menu-btn").onClick(Action(this.onBackToMenu));
        m_state = GameState::LevelFailed;
    }

    // Play Again / Retry: reload the level fresh (enterPlaying resets score + resumes time).
    void onPlayAgain() { enterPlaying(); }
    void onBackToMenu() { showMainMenu(); }
}
