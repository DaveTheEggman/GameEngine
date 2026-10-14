// Player - a guest someone plays. On a guest's entity like any other (a navigation agent and an
// Instance Member of a costume), so it looks like one. It moves where the stick points, relative
// to where the camera looks, by steering its agent a step ahead each frame: the crowd makes way
// round it and it round the crowd, as guests do for each other. A guest's stroll by default;
// Sprint is faster (and marks you, Ball.as); Dance, held, dances; Tag tells the ball (Ball.as
// decides who is in reach). It wears its costume on a child, one of six looks (Ball.as picks), so
// the clip goes to each look. Its `activity` is a guest's.

const int kIdle = 0; // the clips, in the Guests entity's instanced skinning order
const int kWalk = 1;
const int kTalk = 2;
const int kDance = 3;

class Player
{
    private Entity@ self;

    [null, "The camera whose facing makes forward"] Entity@ camera;
    [1.0, "Walking speed (m/s): a guest's, the Walk clip's"] float walkSpeed;
    [3.0, "Sprinting speed (m/s)"] float sprintSpeed;
    [1.2, "How far ahead the agent is steered (m)"] float lead;
    [0, "What the player is doing (0 idle, 1 walk, 2 talk, 3 dance): the clip it plays", replicated] int activity;
    [false, "Sprinting now (a hunter sees it)", replicated] bool sprinting;
    [0, "Its seat at the ball: the hunter it is in Ball.as"] int seat;

    private float m_yaw = 0.0f;
    private bool m_moving = false;

    Player(Entity@ entity) { @self = entity; }

    void onUpdate(double dt)
    {
        float d = float(dt);
        NavAgentComponent@ agent = NavAgentComponent::of(self);
        Float2 stick = Input::value2D("Move");
        float amount = Math::Sqrt(stick.x * stick.x + stick.y * stick.y);
        sprinting = false;
        if (amount >= 0.1f && camera !is null && camera.isValid())
        {
            if (amount > 1.0f)
            {
                stick = Float2(stick.x / amount, stick.y / amount);
            }
            // The camera's facing along the floor: forward, and right of it.
            Float3 look = Quaternion::RotateVector(camera.rotation(), Float3(0.0f, 0.0f, -1.0f));
            float flat = Math::Max(Math::Sqrt(look.x * look.x + look.z * look.z), 0.0001f);
            Float3 forward = Float3(look.x / flat, 0.0f, look.z / flat);
            Float3 right = Float3(-forward.z, 0.0f, forward.x);
            float dx = forward.x * stick.y + right.x * stick.x;
            float dz = forward.z * stick.y + right.z * stick.x;
            sprinting = Input::isDown("Sprint");
            float speed = sprinting ? sprintSpeed : walkSpeed;
            Float3 p = self.worldPosition();
            agent.navigateAt(p.x + dx * lead, p.y, p.z + dz * lead, speed, 0.05f);
            m_moving = true;
            face(dx, dz, d);
        }
        else if (m_moving)
        {
            agent.stop();
            m_moving = false;
        }
        if (Input::wasPressed("Tag"))
        {
            self.scene.events.emit("Tag", seat);
        }
        if (m_moving)
        {
            activity = kWalk;
        }
        else
        {
            activity = Input::isDown("Dance") ? kDance : kIdle;
        }
        Entity@ child = self.firstChild();
        while (child !is null && child.isValid())
        {
            InstanceMemberComponent@ look = InstanceMemberComponent::of(child);
            if (look !is null)
            {
                look.clip = uint(activity);
            }
            @child = child.nextSibling();
        }
    }

    // Face where it goes (the model faces +Z), turning the short way round.
    private void face(float dx, float dz, float d)
    {
        float want = Math::Atan2(dx, dz);
        float delta = want - m_yaw;
        while (delta > 3.14159265f) delta -= 6.2831853f;
        while (delta < -3.14159265f) delta += 6.2831853f;
        m_yaw += delta * Math::Min(10.0f * d, 1.0f);
        self.setRotationEuler(0.0f, m_yaw * 57.2957795f, 0.0f);
    }
}
