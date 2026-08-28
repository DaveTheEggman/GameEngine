// Level - the per-scene script for PaperKid's driving level (P1-6). Set this class on MainScene's
// Scene Script settings. It owns the countdown timer + the delivery quota, harvests the SCENE bus,
// and relays to the RUN bus what the Game (PaperKidGame) needs to hear:
//
//   - Subscriber.as emits scene "Delivered"(points) when a paper lands in a zone -> the Level counts
//     it, relays "Delivered"(points) to the run bus (the Game adds to score), and emits "QuotaMet"
//     to the run bus once enough deliveries land.
//   - the timer ticks ONLY while the scene simulates (per-scene time - paused freezes it); at 0 it
//     emits "TimeUp" to the run bus.
//
// The Game drives LevelCleared / LevelFailed off those run-bus events (its on<Event> inbox). This
// scene->run relay is explicit on purpose: there is no implicit scene->run bridge.
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

    Level(Scene@ s) { @scene = s; }

    void onStart()
    {
        m_timeLeft = timeLimit;
        m_delivered = 0;
        m_ended = false;
    }

    // Gameplay dt; runs only while the scene simulates.
    void onUpdate(double dt)
    {
        if (m_ended)
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

    // Scene bus: a subscriber scored a delivery (Subscriber.as emits "Delivered", points).
    void onDelivered(int points)
    {
        if (m_ended)
        {
            return;
        }
        m_delivered += 1;
        run::events().emit("Delivered", points); // relay to the Game for scoring
        if (m_delivered >= quota)
        {
            m_ended = true;
            run::events().emit("QuotaMet", m_delivered);
        }
    }

    void onStop() {}
}
