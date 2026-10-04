// Pedestrian - wanders the block on the navmesh: picks a spot around the ring road (on a verge
// or across it), walks there, picks another. The Level sets the walking speed ("PedestrianSpeed")
// and says where the ring road runs ("BlockRing"), which sets the band the walks stay in.
//
// The figure (Models/Town/PedestrianModel) turns toward where it walks, easing round rather than
// snapping, and its Walk clip (two 0.65 m steps a second) plays at the pace it walks.

// The pedestrian's walk (Tools/blender/pedestrian.py): 1.3 m of pavement a second at speed 1.
Guid kWalkClip = Guid("a17a3a0d-f0e4-4d8e-b39d-3ead681d7273");
const float kWalkMetres = 1.3f;

class Pedestrian
{
    private Entity@ self;

    [1.6, "Walking speed until the Level sets one (m/s)"] float speed;
    [18.0, "Nearest the middle a walk may lead (m)"] float inner;
    [31.0, "Farthest from the middle a walk may lead (m)"] float outer;

    private bool m_walking = false;
    private Entity@ m_figure;
    private float m_yaw = 0.0f; // radians; 0 faces +Z

    Pedestrian(Entity@ entity) { @self = entity; }

    void onStart()
    {
        NavAgentComponent::of(self).setSpeed(speed);
        pickTarget();
        @m_figure = self.findChildByName("PedestrianModel");
        if (m_figure !is null && m_figure.isValid())
        {
            SceneAnimation anim = SceneAnimation::of(self.scene);
            anim.setClip(m_figure, kWalkClip);
            anim.play(m_figure);
        }
    }

    void onPedestrianSpeed(float s)
    {
        speed = s;
        NavAgentComponent::of(self).setSpeed(speed);
    }

    void onBlockRing(float ring)
    {
        inner = ring - 6.0f;
        outer = ring + 7.0f;
        pickTarget();
    }

    void onUpdate(double dt)
    {
        NavAgentComponent@ agent = NavAgentComponent::of(self);
        if (m_walking && agent.finished())
        {
            pickTarget();
        }
        // Face the way the walk goes (turning the short way round, a few times a second), and
        // step at the pace of it; standing, the figure stands still.
        float vx = agent.velocityX();
        float vz = agent.velocityZ();
        float pace = Math::Sqrt(vx * vx + vz * vz);
        if (pace > 0.05f)
        {
            float turn = Math::Atan2(vx, vz) - m_yaw;
            while (turn > 3.14159f) { turn -= 6.28318f; }
            while (turn < -3.14159f) { turn += 6.28318f; }
            float ease = 8.0f * float(dt);
            m_yaw += turn * ((ease < 1.0f) ? ease : 1.0f);
            self.setRotationEuler(0.0f, Math::RadiansToDegrees(m_yaw), 0.0f);
        }
        if (m_figure !is null && m_figure.isValid())
        {
            SkeletalAnimationComponent::of(m_figure).speed = pace / kWalkMetres;
        }
    }

    // A point in the band around the ring road, on one of its four sides.
    private void pickTarget()
    {
        float along = Random::range(-outer, outer);
        float across = Random::range(inner, outer);
        if (Random::boolean())
        {
            across = -across;
        }
        float x = along;
        float z = across;
        if (Random::boolean())
        {
            x = across;
            z = along;
        }
        NavAgentComponent::of(self).navigate(x, 0.0f, z);
        m_walking = true;
    }
}
