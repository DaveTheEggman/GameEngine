// PaperKidGame - the Game-tier orchestrator (P0: the skeleton + screen flow).
//
// This is the project's startup script (Project.xml startupScriptId), run by the play-in-editor
// GameEditorPage as the reserved `Game` tier: launch() boots it, update(dt) ticks it, exit() tears
// it down, and on<Event>(...) methods harvest the run bus. It drives the whole screen/state flow
// over the run + ui facades; there is no gameplay yet (that is P1+).
//
// Screens are authored UIDocuments (Sources/screens/*.sml), pushed by guid via ui::push; their
// id-tagged buttons are wired to handlers with button.onClick(Action(this.handler)). The empty
// "Playing" level is a scene loaded with run::loadScene.
//
// Asset guids are referenced as Guid(high, low) - the two 64-bit halves of the asset's UUID (there
// is no string Guid ctor for scripts yet). These MUST match the guids in the matching .xasset
// envelopes. UUID -> halves: the first 8 bytes are `high`, the last 8 are `low`.

// --- authored asset guids (keep in sync with the .xasset envelopes) ---
// main-menu  ac96b003-5b7c-433f-896c-489befb6e2c2
Guid kMainMenuDoc = Guid(0xac96b0035b7c433f, 0x896c489befb6e2c2);
// pause      985eb393-4110-4fc4-9742-7bac60ca136d
Guid kPauseDoc = Guid(0x985eb39341104fc4, 0x97427bac60ca136d);
// settings   d30677d4-cb28-4ec1-958a-f8046e0c67a5
Guid kSettingsDoc = Guid(0xd30677d4cb284ec1, 0x958af8046e0c67a5);
// MainScene  855ffed4-4da7-4fa0-9756-a95c6c842890
Guid kPlayingLevel = Guid(0x855ffed44da74fa0, 0x9756a95c6c842890);

enum GameState
{
    Booting,
    MainMenu,
    Playing,
    Paused,
    Settings
}

class Game
{
    GameState m_state = GameState::Booting;
    // Where "Back" returns from the Settings screen (Settings opens from both Main menu and Pause).
    GameState m_settingsReturn = GameState::MainMenu;

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
                Log::info("Pause pressed");
                showPause();
            }
            else if (m_state == GameState::Paused)
            {
                Log::info("Resume pressed");
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
        ui::clear();
        Screen menu = ui::push(kMainMenuDoc);
        menu.findButton("start-btn").onClick(Action(this.onStartGame));
        menu.findButton("settings-btn").onClick(Action(this.onOpenSettingsFromMenu));
        menu.findButton("quit-btn").onClick(Action(this.onQuitGame));
        m_state = GameState::MainMenu;
    }

    void enterPlaying()
    {
        // Load the (empty) level and drop all menu screens so the scene shows through.
        run::loadScene(kPlayingLevel);
        ui::clear();
        m_state = GameState::Playing;
    }

    void showPause()
    {
        // Pause OVERLAYS the running scene (a modal screen); the scene keeps its state, frozen.
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
}
