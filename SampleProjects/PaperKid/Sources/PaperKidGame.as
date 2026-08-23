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
// Asset guids are referenced as Guid("...") - the canonical UUID string, copied straight from the
// matching .xasset envelope. These MUST match the envelopes; a malformed string parses to Nil.

// --- authored asset guids (keep in sync with the .xasset envelopes) ---
Guid kMainMenuDoc = Guid("ac96b003-5b7c-433f-896c-489befb6e2c2");   // main-menu
Guid kPauseDoc = Guid("985eb393-4110-4fc4-9742-7bac60ca136d");      // pause
Guid kSettingsDoc = Guid("d30677d4-cb28-4ec1-958a-f8046e0c67a5");   // settings
Guid kPlayingLevel = Guid("855ffed4-4da7-4fa0-9756-a95c6c842890");  // MainScene

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
