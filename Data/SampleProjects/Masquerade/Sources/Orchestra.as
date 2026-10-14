// Orchestra - on the dais: plays the evening's cues in turn, each for its while, and tells the
// ball (the "Cue" event: 0 stroll, 1 gather, 2 dance). Guests follow the cue (Guest.as); a
// player who does not stands out. The cue is replicated, so a joining player hears the same music.

const int kStroll = 0;
const int kGather = 1;
const int kDanceCue = 2;

class Orchestra
{
    private Entity@ self;

    [40.0, "Seconds of strolling"] float strollSeconds;
    [20.0, "Seconds of gathering to talk"] float gatherSeconds;
    [25.0, "Seconds of dancing"] float danceSeconds;
    [0, "The cue playing now (0 stroll, 1 gather, 2 dance)", replicated] int cue;

    private float m_left = 0.0f;
    private int m_told = -1;
    private int m_step = 0;

    Orchestra(Entity@ entity) { @self = entity; }

    void onStart()
    {
        m_left = strollSeconds;
    }

    void onUpdate(double dt)
    {
        if (!Net::isClient())
        {
            m_left -= float(dt);
            if (m_left <= 0.0f)
            {
                // The evening's order: stroll, gather, stroll, dance, round again.
                m_step = (m_step + 1) % 4;
                cue = (m_step == 1) ? kGather : ((m_step == 3) ? kDanceCue : kStroll);
                m_left = (cue == kGather) ? gatherSeconds : ((cue == kDanceCue) ? danceSeconds : strollSeconds);
            }
        }
        if (cue != m_told) // every peer tells its own ball when the (replicated) cue changes
        {
            m_told = cue;
            self.scene.events.emit("Cue", cue);
        }
    }
}
