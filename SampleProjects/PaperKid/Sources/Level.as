// Level - the per-scene script for PaperKid's driving level (P1-6). Set this class on MainScene's
// Scene Script settings. It owns the countdown timer + the delivery quota.
//
// KEY: in a GameInstance the SCENE bus IS the run bus (SetSceneEventBus(&m_runEvents)) - ONE bus per
// run scope. So a scene event a behavior emits is already heard by the Game's on<Event> inbox; there
// is NO relay to do (and re-emitting the same name loops back into this handler). The Level therefore:
//   - COUNTS the Subscriber's "Delivered"(points) (the Game also hears it directly for score), and on
//     reaching quota emits the NEW signal "QuotaMet";
//   - ticks the countdown ONLY while the scene simulates (per-scene time - pause/timescale 0 freezes
//     it), emitting the NEW signal "TimeUp" at 0.
// The Game drives LevelCleared / LevelFailed off "QuotaMet" / "TimeUp".
//
// (Out-of-papers as a fail condition is deferred: a just-thrown paper is still in flight and may yet
// deliver, so "no papers left" cannot fail immediately - that needs in-flight tracking, a follow-up.)

class Level
{
    private Scene@ scene;

    // ---- level data (inspector-authored) ----
    [90.0, "Time limit (seconds)"]      float timeLimit;
    [3, "Deliveries needed to clear"]   int quota;

    // ---- runtime state (no metadata => not properties) ----
    private float m_timeLeft = 0.0f;
    private int m_delivered = 0;
    private bool m_ended = false;
    private bool m_started = false;

    Level(Scene@ s) { @scene = s; }

    void onStart()
    {
        // timeLimit/quota are real inspector properties now: the harvested default (or an
        // inspector-authored override) is applied to the fields BEFORE onStart, exactly like a
        // behavior's properties. Just seed the countdown from the authored value.
        m_timeLeft = timeLimit;
        m_delivered = 0;
        m_ended = false;
        m_started = true;
    }

    // Gameplay dt; runs only while the scene simulates.
    void onUpdate(double dt)
    {
        // Do NOT count down before onStart has seeded m_timeLeft (0 would insta-fail).
        if (!m_started || m_ended)
        {
            return;
        }
        m_timeLeft -= float(dt);
        if (m_timeLeft <= 0.0f)
        {
            m_timeLeft = 0.0f;
            m_ended = true;
            run::events().emit("TimeUp", 0);
        }
    }

    // The scene bus IS this run's bus, so the Game already hears the Subscriber's "Delivered" directly
    // - do NOT re-emit it (that fed the same bus and looped back into this handler until quota). The
    // Level only COUNTS deliveries and, on reaching quota, emits the NEW "QuotaMet" signal.
    void onDelivered(int points)
    {
        if (m_ended)
        {
            return;
        }
        m_delivered += 1;
        if (m_delivered >= quota)
        {
            m_ended = true;
            run::events().emit("QuotaMet", m_delivered);
        }
    }

    void onStop() {}
}
