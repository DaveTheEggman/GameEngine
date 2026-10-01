// TimedLife - removes its entity a while after it appears: what a one-shot effect's prefab
// carries, so whoever spawns it can forget it.
class TimedLife
{
    private Entity@ self;

    [3.0, "Seconds before the entity removes itself"] float seconds;

    private float m_age = 0.0f;

    TimedLife(Entity@ entity) { @self = entity; }

    void onUpdate(double dt)
    {
        m_age += float(dt);
        if (m_age >= seconds)
        {
            self.destroy();
        }
    }
}
