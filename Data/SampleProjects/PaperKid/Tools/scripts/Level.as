// Level - a block's rules and numbers: the countdown, the papers, the deliveries toward the quota.
// The block's scene sets the numbers on this script's properties, so each block carries its own.
//
// In a run the scene bus IS the run bus: a behavior's "Delivered" (Subscriber) and "PaperThrown"
// (Bike) reach this script and the Game alike. This script turns them into the level's outcome -
// "QuotaMet", or "LevelFailed" with the reason (0 time, 1 papers) - and tells the bike how many
// papers are left ("PapersLeft"). It writes the HUD's time, papers and deliveries. "QuotaMet" carries
// the whole seconds left, the Game's time bonus.
//
// The block's traffic is set here too: on the first frame (every behavior has started by then) it
// tells the cars and pedestrians their speeds, and the pedestrians where the ring road runs. A crash ("BikeCrashed", from the Bike) costs time.
class Level
{
    private Scene@ scene;

    [120.0, "Time limit (s)"] float timeLimit;
    [4, "Deliveries to clear the block"] int quota;
    [8, "Papers for the block"] int papers;
    [3.0, "After the last paper, how long it may still land (s)"] float paperGrace;
    [6.0, "Car speed (m/s)"] float trafficSpeed;
    [1.6, "Pedestrian walking speed (m/s)"] float pedestrianSpeed;
    [5.0, "Seconds a crash costs"] float crashPenalty;
    [24.0, "The ring road's distance from the middle (m)"] float ring;

    private float m_timeLeft = 0.0f;
    private int m_papersLeft = 0;
    private int m_delivered = 0;
    private bool m_ended = false;
    private float m_graceLeft = -1.0f; // counting down once the last paper is thrown
    private bool m_trafficSet = false;

    Level(Scene@ s) { @scene = s; }

    void onStart()
    {
        m_timeLeft = timeLimit;
        m_papersLeft = papers;
        scene.events.emit("PapersLeft", m_papersLeft);
        updateHud();
    }

    void onUpdate(double dt)
    {
        if (!m_trafficSet)
        {
            m_trafficSet = true;
            scene.events.emit("TrafficSpeed", trafficSpeed);
            scene.events.emit("PedestrianSpeed", pedestrianSpeed);
            scene.events.emit("BlockRing", ring);
        }
        if (m_ended)
        {
            return;
        }
        m_timeLeft -= float(dt);
        if (m_timeLeft <= 0.0f)
        {
            m_timeLeft = 0.0f;
            end(false, 0);
            return;
        }
        if (m_graceLeft >= 0.0f)
        {
            m_graceLeft -= float(dt);
            if (m_graceLeft < 0.0f)
            {
                end(false, 1);
                return;
            }
        }
        updateHud();
    }

    void onBikeCrashed(int count)
    {
        if (m_ended)
        {
            return;
        }
        m_timeLeft -= crashPenalty;
        updateHud();
    }

    void onPaperThrown(int count)
    {
        if (m_ended || m_papersLeft <= 0)
        {
            return;
        }
        m_papersLeft -= 1;
        scene.events.emit("PapersLeft", m_papersLeft);
        if (m_papersLeft == 0)
        {
            m_graceLeft = paperGrace;
        }
        updateHud();
    }

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
            end(true, 0);
        }
    }

    private void end(bool cleared, int reason)
    {
        m_ended = true;
        updateHud();
        if (cleared)
        {
            run::events().emit("QuotaMet", int(m_timeLeft));
        }
        else
        {
            run::events().emit("LevelFailed", reason);
        }
    }

    private void updateHud()
    {
        ui::findLabel("hud-time").setText("" + int(m_timeLeft + 0.99f));
        ui::findLabel("hud-papers").setText("" + m_papersLeft);
        ui::findLabel("hud-deliveries").setText("" + m_delivered + " / " + quota);
    }
}
