// Fx - a one-shot particle burst (an Fx* prefab: its effect bursts once when it is spawned), gone
// once the burst has played out. The scripts spawn these where things happen: confetti over a
// cleared block, a sparkle on a delivery, dust off a crash, a puff where a paper lands.
class Fx
{
    private Entity@ self;

    [4.0, "Seconds before the burst is gone"] float lifetime;

    private float m_age = 0.0f;

    Fx(Entity@ entity) { @self = entity; }

    void onUpdate(double dt)
    {
        m_age += float(dt);
        if (m_age >= lifetime)
        {
            self.destroy();
        }
    }
}
