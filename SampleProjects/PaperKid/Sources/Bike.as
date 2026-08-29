// Bike - the player's ride.
//
// A per-entity script BEHAVIOR attached to the bike entity (which carries a CharacterComponent =
// Jolt CharacterVirtual, kinematic arcade feel - no ragdoll). Each frame it reads the "Move" axis
// (WASD via the Composite2D binding in DefaultInputMap): Y = throttle/brake, X = steer. It keeps a
// heading (yaw) and a scalar speed, turns the heading, then drives the character with a horizontal
// velocity pointing along that heading. The third-person camera (FollowCamera.as) reads this
// entity's transform to trail behind.
//
// Facing is derived from the heading with the reflected math surface: a yaw quaternion about +Y
// rotates local forward (+Z) into the world velocity direction, and the SAME yaw sets the mesh
// rotation via setRotationEuler - so the model always points where it moves.
//
// Tunables are [metadata]-annotated FIELDS: they show up in the inspector, authored per-entity.

// The paper prefab thrown on the Throw action. Copied from Content/Scenes/Paper.xasset's
// guid - keep in sync with that envelope.
Guid kPaperPrefab = Guid("6eccb2d5-b150-4cc7-ba67-1a1c09383be4");

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

    // ---- throwing ----
    [60.0, "Throw impulse (launch strength; scales with paper mass)"] float throwImpulse;
    [0.65, "Throw arc (upward bias)"]                 float throwArc;
    [0.6, "Auto-aim strength (0 = straight, 1 = locked on)"] float autoAim;
    [18.0, "Auto-aim range (m)"]                      float aimRange;
    [10, "Papers per level"]                          int startingPapers;
    [2, "Subscriber collision group"]                 int subscriberGroup;

    // ---- aim preview: a debug-drawn arc of where the throw will go ----
    [10.0, "Aim preview launch speed (visual only, m/s)"] float aimPreviewSpeed;
    [1.5, "Aim preview duration (s)"]                     float aimPreviewTime;

    // ---- runtime state (no metadata => not properties) ----
    private float m_heading = 0.0f; // yaw in RADIANS (0 = facing world +Z)
    private float m_speed = 0.0f;   // signed forward speed (negative = reversing)
    private int m_papers = 0;       // papers remaining (seeded from startingPapers in onStart)
    // The throw aim, recomputed each frame (for the preview) and reused by a throw.
    private float m_aimX = 0.0f;    // horizontal aim direction (unit XZ)
    private float m_aimZ = 1.0f;
    private bool m_hasTarget = false;      // the auto-aim locked a subscriber zone this frame
    private Float3 m_targetPos = Float3(0.0f, 0.0f, 0.0f);

    Bike(Entity@ entity) { @self = entity; }

    void onStart()
    {
        m_papers = startingPapers;
        updatePapersHud();
    }

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

        // Preview the throw: recompute the aim + draw its projected arc each frame (papers permitting).
        // Immediate-mode debug draw is cleared every frame, so it must be re-issued from onUpdate.
        if (m_papers > 0)
        {
            computeAim();
            drawAimPreview();
        }

        // Throw a paper on the (edge-triggered) Throw action, papers permitting. A paper is only
        // consumed when the throw actually spawned one (a failed prefab resolve must not burn
        // papers toward the OutOfPapers fail condition with nothing thrown).
        if (m_papers > 0 && Input::wasPressed("Throw") && throwPaper())
        {
            m_papers -= 1;
            updatePapersHud();
            // On the LAST paper, tell the Level (it grace-waits for this one to land, then fails if
            // quota is still unmet). The scene bus IS the run bus, so the Level's onOutOfPapers hears it.
            if (m_papers == 0)
            {
                self.scene.events.emit("OutOfPapers", 0);
            }
        }
    }

    // Mirror the remaining paper count into the overlay HUD (a no-op when the HUD is not shown).
    private void updatePapersHud()
    {
        ui::findLabel("hud-papers").setText("Papers " + m_papers);
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

    // Compute the throw's HORIZONTAL aim: the bike's forward heading, biased by a soft auto-aim toward
    // the nearest subscriber delivery zone in front. Fills m_aimX/m_aimZ (unit XZ) and the locked-
    // target state (m_hasTarget/m_targetPos). Called each frame for the preview and before a throw.
    private void computeAim()
    {
        Float3 pos = self.worldPosition();
        float fx = Math::Sin(m_heading); // bike forward XZ (matches applyMotion: forward = (sin h, 0, cos h))
        float fz = Math::Cos(m_heading);

        float aimX = fx;
        float aimZ = fz;
        m_hasTarget = false;

        // Overlap the subscriber-group zones nearby; bias toward the nearest one that is IN FRONT.
        int mask = 1 << subscriberGroup;
        array<Entity@>@ zones =
            ScenePhysics::of(self.scene).overlapSphere(pos.x, pos.y, pos.z, aimRange, mask);
        float bestDist = aimRange * aimRange + 1.0f;
        for (uint i = 0; i < zones.length(); i++)
        {
            Float3 zp = zones[i].worldPosition();
            float dx = zp.x - pos.x;
            float dz = zp.z - pos.z;
            float dist2 = dx * dx + dz * dz;
            if (dist2 < 0.0001f) { continue; }
            float len = Math::Sqrt(dist2);
            float ndx = dx / len;
            float ndz = dz / len;
            if (ndx * fx + ndz * fz > 0.1f && dist2 < bestDist) // in front + nearer than the best so far
            {
                bestDist = dist2;
                aimX = fx + (ndx - fx) * autoAim; // lerp forward -> zone by the auto-aim strength
                aimZ = fz + (ndz - fz) * autoAim;
                m_hasTarget = true;
                m_targetPos = zp;
            }
        }
        float l = Math::Sqrt(aimX * aimX + aimZ * aimZ);
        if (l > 0.0001f) { aimX /= l; aimZ /= l; }
        m_aimX = aimX;
        m_aimZ = aimZ;
    }

    // Debug-draw the throw's projected path: sample the ballistic arc under the scene's gravity and
    // connect it with line segments, plus a marker on the locked target. The launch SPEED here is a
    // visual tunable (aimPreviewSpeed) - the real throw is an impulse whose speed depends on the paper
    // mass, so dial aimPreviewSpeed to match the felt throw. Immediate-mode: re-issued every frame.
    private void drawAimPreview()
    {
        Float3 pos = self.worldPosition();
        float fx = Math::Sin(m_heading);
        float fz = Math::Cos(m_heading);
        Float3 origin = Float3(pos.x + fx, pos.y + 1.2f, pos.z + fz); // matches the throw spawn point

        // Launch velocity = throw direction (unit horizontal aim + upward arc) at the preview speed.
        float mag = Math::Sqrt(1.0f + throwArc * throwArc);
        float vx = m_aimX / mag * aimPreviewSpeed;
        float vy = throwArc / mag * aimPreviewSpeed;
        float vz = m_aimZ / mag * aimPreviewSpeed;
        float g = ScenePhysics::of(self.scene).gravityY();

        DebugDraw@ dbg = DebugDraw::of(self.scene);
        int steps = 24;
        float dt = aimPreviewTime / float(steps);
        // Track the previous point as PLAIN FLOATS (reassigning a Float3 in the loop does not draw).
        float prevX = origin.x;
        float prevY = origin.y;
        float prevZ = origin.z;
        for (int i = 1; i <= steps; i++)
        {
            float t = dt * float(i);
            float px = origin.x + vx * t;
            float py = origin.y + vy * t + 0.5f * g * t * t;
            float pz = origin.z + vz * t;
            dbg.line(prevX, prevY, prevZ, px, py, pz, 1.0f, 0.85f, 0.1f); // amber arc
            prevX = px;
            prevY = py;
            prevZ = pz;
            if (py < 0.0f) { break; } // stop at the ground plane
        }
        if (m_hasTarget)
        {
            dbg.sphere(m_targetPos.x, m_targetPos.y, m_targetPos.z, 0.6f, 0.2f, 1.0f, 0.3f); // locked (green)
        }
    }

    // Spawn + launch a paper along the freshly-computed aim (horizontal aim + upward arc).
    // Returns whether a paper actually spawned (the caller only consumes one on success).
    private bool throwPaper()
    {
        computeAim();
        Float3 pos = self.worldPosition();
        float fx = Math::Sin(m_heading);
        float fz = Math::Cos(m_heading);

        // Spawn in front of + above the bike so the paper clears it, then launch (horizontal + arc).
        Float3 origin = Float3(pos.x + fx, pos.y + 1.2f, pos.z + fz);
        Entity@ paper = self.scene.spawn(kPaperPrefab, origin.x, origin.y, origin.z);
        if (paper is null || !paper.isValid())
        {
            return false;
        }
        // (m_aimX, throwArc, m_aimZ) with unit horizontal -> normalize so throwImpulse is the magnitude.
        float mag = Math::Sqrt(1.0f + throwArc * throwArc);
        ScenePhysics::of(self.scene).applyImpulse(paper, m_aimX / mag * throwImpulse,
                                                  throwArc / mag * throwImpulse,
                                                  m_aimZ / mag * throwImpulse);
        return true;
    }
}
