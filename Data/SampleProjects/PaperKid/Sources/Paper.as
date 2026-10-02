// Paper - a thrown newspaper. Entering any trigger, it tells that trigger's entity it arrived
// ("PaperArrived"); only a subscriber's delivery zone answers, so nothing here needs to know which
// triggers are porches. Gone a while after it is thrown, delivered or not.
class Paper
{
    private Entity@ self;

    [6.0, "Seconds before the paper is gone"] float lifetime;

    private float m_age = 0.0f;

    Paper(Entity@ entity) { @self = entity; }

    void onTriggerEnter(Entity@ other)
    {
        other.send("PaperArrived", 0);
    }

    void onUpdate(double dt)
    {
        m_age += float(dt);
        if (m_age >= lifetime)
        {
            self.destroy();
        }
    }
}
