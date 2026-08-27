// Bike - the player's ride (P1-2 of the driving slice).
//
// A per-entity script BEHAVIOR attached to the bike entity (which carries a CharacterComponent =
// Jolt CharacterVirtual, kinematic arcade feel - no ragdoll). Each frame it reads the "Move" axis
// (WASD via the Composite2D binding in DefaultInputMap): Y = throttle/brake, X = steer. It keeps a
// heading (yaw) and a scalar speed, turns the heading, then drives the character with a horizontal
// velocity pointing along that heading. The third-person camera (P1-3, FollowCamera.as) reads this
// entity's transform to trail behind.
//
// Facing is derived from the heading with the reflected math surface: a yaw quaternion about +Y
// rotates local forward (+Z) into the world velocity direction, and the SAME yaw sets the mesh
// rotation via setRotationEuler - so the model always points where it moves.
//
// Tunables are [metadata]-annotated FIELDS: they show up in the inspector, authored per-entity.

class Bike
{
    private Entity@ self;

    // ---- tunables (inspector-authored behavior properties) ----
    [9.0, "Top forward speed (m/s)"]           float maxSpeed;
    [3.5, "Top reverse speed (m/s)"]           float reverseSpeed;
    [14.0, "Throttle ramp (m/s^2)"]            float acceleration;
    [22.0, "Active brake / reverse ramp (m/s^2)"] float braking;
    [8.0, "Roll-down when coasting (m/s^2)"]   float coastDeceleration;
    [130.0, "Yaw rate at full speed (deg/s)"]  float turnSpeedDegrees;
    [0.25, "Steering authority floor (0..1)"]  float minSteerFraction;

    // ---- runtime state (no metadata => not properties) ----
    private float m_heading = 0.0f; // yaw in RADIANS (0 = facing world +Z)
    private float m_speed = 0.0f;   // signed forward speed (negative = reversing)

    Bike(Entity@ entity) { @self = entity; }

    void onUpdate(double dt)
    {
        float d = float(dt);
        if (d <= 0.0f)
        {
            return;
        }

        float throttle = Input::valueY("Move"); // W = +1 (forward), S = -1 (back)
        float steer = Input::valueX("Move");     // D = +1 (right),   A = -1 (left)

        updateSpeed(throttle, d);
        updateHeading(steer, d);
        applyMotion();
    }

    // Ramp the signed speed toward the throttle intent, clamped to the forward/reverse caps.
    private void updateSpeed(float throttle, float d)
    {
        if (throttle > 0.0f)
        {
            // Accelerating forward (brake harder first if we were reversing).
            float rate = (m_speed < 0.0f) ? braking : acceleration;
            m_speed += throttle * rate * d;
        }
        else if (throttle < 0.0f)
        {
            // Braking, then reversing.
            float rate = (m_speed > 0.0f) ? braking : acceleration;
            m_speed += throttle * rate * d;
        }
        else
        {
            // Coast toward a stop.
            if (m_speed > 0.0f)
            {
                m_speed -= coastDeceleration * d;
                if (m_speed < 0.0f) { m_speed = 0.0f; }
            }
            else if (m_speed < 0.0f)
            {
                m_speed += coastDeceleration * d;
                if (m_speed > 0.0f) { m_speed = 0.0f; }
            }
        }

        if (m_speed > maxSpeed) { m_speed = maxSpeed; }
        if (m_speed < -reverseSpeed) { m_speed = -reverseSpeed; }
    }

    // Turn the heading; steering authority scales with how fast we are going (a parked bike barely
    // turns), and inverts while reversing so backing up steers the way a vehicle actually does.
    private void updateHeading(float steer, float d)
    {
        if (steer == 0.0f || m_speed == 0.0f)
        {
            return;
        }

        float speedFraction = Math::Abs(m_speed) / maxSpeed;
        if (speedFraction > 1.0f) { speedFraction = 1.0f; }
        if (speedFraction < minSteerFraction) { speedFraction = minSteerFraction; }

        float direction = (m_speed >= 0.0f) ? 1.0f : -1.0f;
        float turnRate = Math::DegreesToRadians(turnSpeedDegrees);
        // A positive yaw about +Y turns local forward (+Z) toward +X, which is screen-LEFT from the
        // trailing camera - so a positive steer (D = right) must DECREASE the heading to turn right.
        // (direction already inverts this while reversing, so backing up steers like a real vehicle.)
        m_heading -= steer * direction * turnRate * speedFraction * d;
    }

    // Drive the character along the heading and point the mesh the same way.
    private void applyMotion()
    {
        // Forward vector = yaw quaternion about +Y applied to local forward (+Z).
        Quaternion facing = Quaternion::FromAxisAngle(Float3(0.0f, 1.0f, 0.0f), m_heading);
        Float3 forward = Quaternion::RotateVector(facing, Float3(0.0f, 0.0f, 1.0f));

        CharacterComponent::of(self).move(forward.x * m_speed, forward.z * m_speed);
        self.setRotationEuler(0.0f, Math::RadiansToDegrees(m_heading), 0.0f);
    }
}
