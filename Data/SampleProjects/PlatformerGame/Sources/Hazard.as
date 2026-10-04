// Hazard - spikes and the like: the player touching one is hurt (sent "Hurt"), walking into it or
// landing on it.
//
// Touching is a box, not a sphere: within `radius` across the ground and with the player's centre
// no higher than `touchHeight` above the hazard's origin. A sphere could never be reached: the
// hazard's own collider stops the player at its side, with the player's centre a metre above the
// ground, and stood on top the centre is higher still (the spikes are 1.5 m tall: their centre
// stands 2.5 m above their origin).
class Hazard
{
    private Entity@ self;

    [1.1, "Touch reach across the ground, hazard to player (m)"] float radius;
    [2.6, "Touching while the player's centre is at most this far above the hazard's origin (m): its top plus the player's feet (0.95)"] float touchHeight;

    private Entity@ m_player;

    Hazard(Entity@ entity) { @self = entity; }

    void onStart()
    {
        @m_player = self.scene.find("Player");
    }

    void onUpdate(double dt)
    {
        if (m_player is null || !m_player.isValid())
        {
            return;
        }
        Float3 at = m_player.worldPosition();
        Float3 here = self.worldPosition();
        float dx = at.x - here.x;
        float dz = at.z - here.z;
        float above = at.y - here.y;
        if ((dx * dx + dz * dz < radius * radius) && (above <= touchHeight) && (above > -0.5f))
        {
            m_player.send("Hurt");
        }
    }
}
