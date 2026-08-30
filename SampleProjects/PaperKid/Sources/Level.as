// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Level - the per-scene script for PaperKid's driving level. Set this class on MainScene's Scene
// Script settings. It owns the countdown timer, the delivery quota, the out-of-papers fail rule, and
// it drives the overlay HUD.
//
// KEY: in a GameInstance the SCENE bus IS the run bus (SetSceneEventBus(&m_runEvents)) - ONE bus per
// run scope. So a scene event a behavior emits (Subscriber's "Delivered", Bike's "OutOfPapers") is
// already heard by the Game's on<Event> inbox AND by this Level's on<Event> handlers; there is NO
// relay to do (re-emitting the same name loops back). So the Level reacts to those and emits only NEW
// signals the Game turns into screens:
//   - reaching quota   -> "QuotaMet"    -> Level Cleared
//   - timer hits 0     -> "LevelFailed", 0  (reason: time)
//   - out of papers    -> "LevelFailed", 1  (reason: papers), AFTER a short grace so a paper still in
//                          flight can land and possibly meet quota first.
//
// The HUD (an Overlay screen the Game pushes) is populated by findLabel().setText() - the Level writes
// the timer + delivery count; the Bike writes the paper count. When the HUD is not up (menus), the
// loud-null findLabel makes those writes safe no-ops.

class Level
{
    private Scene@ scene;

    // ---- level data (inspector-authored properties) ----
    [90.0, "Time limit (seconds)"]           float timeLimit;
    [3, "Deliveries needed to clear"]        int quota;
    [3.0, "Grace after the last paper (s)"]  float paperGrace;

    // ---- runtime state (no metadata => not properties) ----
    private float m_timeLeft = 0.0f;
    private int m_delivered = 0;
    private bool m_ended = false;
    private bool m_started = false;
    private bool m_outOfPapers = false; // the last paper has been thrown; grace clock running
    private float m_graceLeft = 0.0f;   // seconds left for an in-flight paper to still deliver

    Level(Scene@ s) { @scene = s; }

    void onStart()
    {
        // timeLimit/quota/paperGrace are real inspector properties: the harvested default (or an
        // inspector-authored override) is applied BEFORE onStart, so just seed from the fields.
        m_timeLeft = timeLimit;
        m_delivered = 0;
        m_ended = false;
        m_started = true;
        m_outOfPapers = false;
        m_graceLeft = 0.0f;
        updateHud();
    }

    // Gameplay dt; runs only while the scene simulates (pause / timescale 0 freezes it).
    void onUpdate(double dt)
    {
        if (!m_started || m_ended)
        {
            return;
        }
        m_timeLeft -= float(dt);
        if (m_timeLeft <= 0.0f)
        {
            m_timeLeft = 0.0f;
            fail(0); // out of time
            return;
        }
        // Out of papers with quota unmet: give a just-thrown paper time to land, then fail.
        if (m_outOfPapers)
        {
            m_graceLeft -= float(dt);
            if (m_graceLeft <= 0.0f)
            {
                fail(1); // out of papers
                return;
            }
        }
        updateHud();
    }

    // A house scored (Subscriber emitted "Delivered"). Count it; at quota, clear the level.
    void onDelivered(int points)
    {
        if (m_ended)
        {
            return;
        }
        m_delivered += 1;
        updateHud();
        if (m_delivered >= quota)
        {
            m_ended = true;
            run::events().emit("QuotaMet", m_delivered);
        }
    }

    // The Bike threw its last paper. Start the grace clock; if it expires with quota unmet, fail.
    void onOutOfPapers(int unused)
    {
        if (m_ended || m_outOfPapers)
        {
            return;
        }
        m_outOfPapers = true;
        m_graceLeft = (paperGrace > 0.0f) ? paperGrace : 3.0f;
    }

    void onStop() {}

    private void fail(int reason)
    {
        m_ended = true;
        run::events().emit("LevelFailed", reason);
    }

    private void updateHud()
    {
        ui::findLabel("hud-timer").setText("Time " + int(m_timeLeft + 0.5f));
        ui::findLabel("hud-deliveries").setText("Delivered " + m_delivered + " / " + quota);
    }
}
