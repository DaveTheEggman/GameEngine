// Enemy - a patroller: it walks back and forth along X between two points, facing the way it
// walks. Touching it hurts the player, unless the player comes down on it from above, which
// defeats it ("EnemyDefeated") and bounces the player off it. On the enemy's entity, whose first
// child is its model.
// FX/FxStompStars: the burst where an enemy was stomped.
Guid kStompStars = Guid("ab197a34-58e6-4f23-add1-0f2a90f00ca6");

// Audio/impactSoft_heavy_000 (Kenney Impact Sounds, CC0): an enemy stomped flat.
Guid kSquashSound = Guid("134e50c9-8003-4065-a81f-69cf5e0d9dc7");

class Enemy
{
    private Entity@ self;

    [3.0, "Patrol half-width along X (m)"] float patrolDistance;
    [2.0, "Walk speed (m/s)"] float speed;
    [1.1, "Touch reach from the enemy's origin to the player's centre (m)"] float radius;
    [0.5, "The player's centre this far above the enemy's counts as a stomp (m)"] float stompHeight;
    ["asset:AnimationClip", "The model's walk clip; none keeps its idle"] Guid@ walkClip;

    private Entity@ m_player;
    private Float3 m_home = Float3(0.0f, 0.0f, 0.0f);
    private float m_offset = 0.0f;
    private float m_direction = 1.0f;
    private float m_lastPlayerY = 0.0f;

    Enemy(Entity@ entity) { @self = entity; }

    void onStart()
    {
        @m_player = self.scene.find("Player");
        m_home = self.position();
        Entity@ model = self.firstChild();
        if (model.isValid() && walkClip !is null && !walkClip.IsNil())
        {
            SceneAnimation animation = SceneAnimation::of(self.scene);
            animation.setClip(model, walkClip);
            animation.play(model);
        }
        if (m_player !is null && m_player.isValid())
        {
            m_lastPlayerY = m_player.worldPosition().y;
        }
    }

    void onUpdate(double delta)
    {
        float dt = float(delta);
        m_offset += m_direction * speed * dt;
        if (m_offset > patrolDistance)
        {
            m_offset = patrolDistance;
            m_direction = -1.0f;
        }
        else if (m_offset < -patrolDistance)
        {
            m_offset = -patrolDistance;
            m_direction = 1.0f;
        }
        self.setPosition(Float3(m_home.x + m_offset, m_home.y, m_home.z));
        self.setRotation(Quaternion::FromAxisAngle(Float3(0.0f, 1.0f, 0.0f),
                                                   (m_direction > 0.0f) ? 1.5708f : -1.5708f));

        if (m_player is null || !m_player.isValid())
        {
            return;
        }
        Float3 at = m_player.worldPosition();
        bool falling = at.y < m_lastPlayerY;
        m_lastPlayerY = at.y;
        Float3 here = self.worldPosition();
        float distance = Float3::Distance(at, here);
        if (distance >= radius + stompHeight)
        {
            return;
        }
        if (falling && (at.y > here.y + stompHeight))
        {
            self.scene.events.emit("EnemyDefeated", 1);
            m_player.send("Bounce");
            ScenePrefabs::of(self.scene).spawn(kStompStars, here + Float3(0.0f, 0.4f, 0.0f));
            Audio::playOneShot(kSquashSound);
            self.destroy();
        }
        else if (distance < radius)
        {
            m_player.send("Hurt");
        }
    }
}
