// Hazard - spikes and the like: the player coming within reach is hurt (sent "Hurt").
class Hazard
{
    private Entity@ self;

    [1.2, "Reach from the hazard's origin to the player's centre (m)"] float radius;

    private Entity@ m_player;

    Hazard(Entity@ entity) { @self = entity; }

    void onStart()
    {
        @m_player = self.scene.find("Player");
    }

    void onUpdate(double dt)
    {
        if (m_player !is null && m_player.isValid()
            && (Float3::Distance(m_player.worldPosition(), self.worldPosition()) < radius))
        {
            m_player.send("Hurt");
        }
    }
}
