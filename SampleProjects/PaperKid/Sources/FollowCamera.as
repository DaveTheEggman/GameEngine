// FollowCamera - the third-person chase camera.
//
// A behavior on the camera entity. The target (the bike) is an [null] Entity@ PROPERTY: pick it in
// the inspector (an entity picker, resolved to a live handle at start) - no fragile name lookup.
// Each frame it springs the camera toward a point behind-and-above the target and aims at it.
// "Behind" is derived from the camera's own lag: as the bike drives forward the camera falls behind,
// so the ground-plane vector from bike back to camera already points the right way - the cam does not
// need to read the bike's heading. Start-as-behavior per the spec; only drop to a native component if
// this reads worse (it does not).
//
// Facing is computed with the reflected math surface: yaw = atan2(dir.x, dir.z), pitch from the
// vertical component, both fed to setRotationEuler (degrees).

class FollowCamera
{
    private Entity@ self;

    // ---- tunables (inspector-authored behavior properties) ----
    [null, "The entity to follow (the bike)"] Entity@ target;
    [7.0, "Distance behind the target (m)"]   float distance;
    [2.2, "Height above the target (m)"]      float height;
    [1.3, "Aim this far above the target (m)"] float lookHeight;
    [4.0, "Position spring rate (higher = snappier)"] float positionSmoothing;

    FollowCamera(Entity@ entity) { @self = entity; }

    void onUpdate(double dt)
    {
        //return;
        float d = float(dt);
        if (d <= 0.0f || target is null || !target.isValid())
        {
            return;
        }

        Float3 targetPos = target.worldPosition();
        Float3 camPos = self.position();

        // Ground-plane direction from target back to the camera = "behind" the direction of travel.
        float backX = camPos.x - targetPos.x;
        float backZ = camPos.z - targetPos.z;
        float flatLen = Math::Sqrt(backX * backX + backZ * backZ);
        float dirX;
        float dirZ;
        if (flatLen < 0.001f)
        {
            dirX = 0.0f; // degenerate at spawn (camera atop target): default straight behind (-Z).
            dirZ = -1.0f;
        }
        else
        {
            dirX = backX / flatLen;
            dirZ = backZ / flatLen;
        }

        Float3 desired = Float3(targetPos.x + dirX * distance, targetPos.y + height,
                                targetPos.z + dirZ * distance);

        // Spring the position toward the desired seat; the moving seat keeps the aim smooth too.
        Float3 newPos = Float3::Lerp(camPos, desired, clamp01(positionSmoothing * d));
        self.setPosition(newPos.x, newPos.y, newPos.z);

        Float3 aim = Float3(targetPos.x, targetPos.y + lookHeight, targetPos.z);
        faceToward(newPos, aim);
    }

    // Point the camera from `from` toward `to` via yaw/pitch (setRotationEuler is in degrees).
    private void faceToward(Float3 from, Float3 to)
    {
        Float3 dir = Float3::Sub(to, from);
        float len = Float3::Length(dir);
        if (len < 0.0001f)
        {
            return;
        }

        // Engine forward is -Z: FromYawPitchRoll gives forward = (-sin(yaw)cos(pitch), -sin(pitch),
        // -cos(yaw)cos(pitch)). To aim forward along `dir`, BOTH atan2 args are negated (yaw = 0 must
        // face -Z, not +Z) - without the negation the camera faces exactly away from the target.
        float yaw = Math::RadiansToDegrees(Math::Atan2(-dir.x, -dir.z));
        // dir.y < 0 (target below the raised camera) -> positive pitch = look down.
        float pitch = Math::RadiansToDegrees(-Math::Asin(dir.y / len));
        self.setRotationEuler(pitch, yaw, 0.0f);
    }

    private float clamp01(float v)
    {
        if (v < 0.0f) { return 0.0f; }
        if (v > 1.0f) { return 1.0f; }
        return v;
    }
}
