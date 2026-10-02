// FollowCamera - the chase camera: it springs toward a seat behind and above the target and aims at
// it. "Behind" is the camera's own lag: as the bike rides on, the camera falls back, so the ground
// direction from the bike back to the camera already points the right way.
class FollowCamera
{
    private Entity@ self;

    [null, "The entity to follow (the bike)"] Entity@ target;
    [8.0, "Distance behind the target (m)"] float distance;
    [3.4, "Height above the target (m)"] float height;
    [1.2, "Aim this far above the target (m)"] float lookHeight;
    [4.5, "Position spring rate (higher = snappier)"] float positionSmoothing;

    FollowCamera(Entity@ entity) { @self = entity; }

    void onStart()
    {
        if (target is null || !target.isValid())
        {
            return;
        }
        // Start straight behind the bike's facing, not wherever the scene put the camera.
        Float3 at = target.worldPosition();
        Float3 back = Quaternion::RotateVector(target.rotation(), Float3(0.0f, 0.0f, -1.0f));
        Float3 seat = Float3(at.x + back.x * distance, at.y + height, at.z + back.z * distance);
        self.setPosition(seat);
        faceToward(seat, Float3(at.x, at.y + lookHeight, at.z));
    }

    void onUpdate(double dt)
    {
        float d = float(dt);
        if (d <= 0.0f || target is null || !target.isValid())
        {
            return;
        }
        // The target's LOCAL position (the bike is a root): physics has already written this
        // frame's interpolated pose there, while worldPosition() is last frame's until the scene
        // updates its transforms after every script, and following it makes the bike shake
        // against the camera at speed.
        Float3 at = target.position();
        Float3 cam = self.position();
        float backX = cam.x - at.x;
        float backZ = cam.z - at.z;
        float flat = Math::Sqrt(backX * backX + backZ * backZ);
        if (flat < 0.001f)
        {
            backX = 0.0f;
            backZ = 1.0f;
            flat = 1.0f;
        }
        Float3 seat = Float3(at.x + backX / flat * distance, at.y + height, at.z + backZ / flat * distance);
        Float3 next = Float3::Lerp(cam, seat, clamp01(positionSmoothing * d));
        self.setPosition(next);
        faceToward(next, Float3(at.x, at.y + lookHeight, at.z));
    }

    // Engine forward is -Z: yaw = atan2(-dir.x, -dir.z); a positive pitch looks up.
    private void faceToward(Float3 from, Float3 to)
    {
        Float3 dir = to - from;
        float len = Math::Sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len < 0.0001f)
        {
            return;
        }
        float yaw = Math::RadiansToDegrees(Math::Atan2(-dir.x, -dir.z));
        float pitch = Math::RadiansToDegrees(Math::Asin(dir.y / len));
        self.setRotationEuler(pitch, yaw, 0.0f);
    }

    private float clamp01(float v)
    {
        if (v < 0.0f) { return 0.0f; }
        if (v > 1.0f) { return 1.0f; }
        return v;
    }
}
