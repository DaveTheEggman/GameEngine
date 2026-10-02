// Pedestrian - wanders the block on the navmesh: picks a spot around the ring road (on a verge
// or across it), walks there, picks another. The Level sets the walking speed ("PedestrianSpeed")
// and says where the ring road runs ("BlockRing"), which sets the band the walks stay in.
class Pedestrian
{
    private Entity@ self;

    [1.6, "Walking speed until the Level sets one (m/s)"] float speed;
    [18.0, "Nearest the middle a walk may lead (m)"] float inner;
    [31.0, "Farthest from the middle a walk may lead (m)"] float outer;

    private bool m_walking = false;

    Pedestrian(Entity@ entity) { @self = entity; }

    void onStart()
    {
        NavAgentComponent::of(self).setSpeed(speed);
        pickTarget();
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
        if (m_walking && NavAgentComponent::of(self).finished())
        {
            pickTarget();
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
