// Coin - a pickup: it spins and bobs, and when the player comes within reach it is collected
// ("CoinCollected") and gone. On the coin's entity, whose first child is the Coin model.
// FX/FxCoinSparkle: the burst where a coin was taken.
Guid kCoinSparkle = Guid("245b459c-4b93-4876-9496-6e4eb3daa145");

// Audio/powerUp7 (Kenney Digital Audio, CC0): the chime of a coin taken.
Guid kCoinSound = Guid("6d963695-67f8-4b7f-9cf4-d234f8034116");

class Coin
{
    private Entity@ self;

    [1.3, "Pickup reach from the player's centre (m)"] float radius;
    [3.0, "Spin rate (rad/s)"] float spinSpeed;
    [0.2, "Bob height (m)"] float bobHeight;
    [1, "Points"] int value;

    private Entity@ m_player;
    private Float3 m_home = Float3(0.0f, 0.0f, 0.0f);
    private float m_time = 0.0f;

    Coin(Entity@ entity) { @self = entity; }

    void onStart()
    {
        @m_player = self.scene.find("Player");
        m_home = self.position();
        self.scene.events.emit("CoinRegistered", 1);
    }

    void onUpdate(double dt)
    {
        m_time += float(dt);
        self.setRotation(Quaternion::FromAxisAngle(Float3(0.0f, 1.0f, 0.0f), m_time * spinSpeed));
        self.setPosition(Float3(m_home.x, m_home.y + Math::Sin(m_time * 2.5f) * bobHeight, m_home.z));
        if (m_player !is null && m_player.isValid()
            && (Float3::Distance(m_player.worldPosition(), self.worldPosition()) < radius))
        {
            self.scene.events.emit("CoinCollected", value);
            ScenePrefabs::of(self.scene).spawn(kCoinSparkle, self.worldPosition());
            Audio::playOneShot(kCoinSound, AudioBus::Effects, 0.7f);
            self.destroy();
        }
    }
}
