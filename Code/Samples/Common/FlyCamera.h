#pragma once
// Shared free-fly camera for the dev samples: WASD/QE move, hold RMB (or Tab to capture) to look,
// Shift to move fast. Provides position + orientation; the sample consumes it as it likes (set a
// camera-entity transform, or build a ViewCamera). The including translation unit must have
// imported draconic.core + draconic.runtime.platform + draconic.runtime.client (this header uses their
// types via fully-qualified names).

namespace draconic::samples {

struct FlyCamera {
    draconic::core::Vec3 position{ 0.0f, 14.0f, 30.0f };
    draconic::core::f32  yaw   = 0.0f;       // 0 => looking down -Z
    draconic::core::f32  pitch = -0.3f;      // tilt down a touch
    bool               mouseCaptured = false;
    draconic::core::f32  moveSpeed = 50.0f, fastSpeed = 200.0f, lookSensitivity = 0.003f;
    draconic::core::f32  zoomSpeed = 3.0f;       // world units per wheel notch (dolly along forward)
    draconic::core::f32  focusDistance = 20.0f;  // pivot distance ahead (Alt+LMB turntable orbit)
    draconic::core::f32  panSensitivity = 0.0015f;   // MMB pan speed (scaled by focus distance)

    [[nodiscard]] draconic::core::Vec3 Up() const {
        return draconic::core::RotateVector(Rotation(), draconic::core::Vec3{ 0.0f, 1.0f, 0.0f });
    }

    // Orientation as a quaternion (yaw about world Y, then pitch about local X). Default forward -Z.
    [[nodiscard]] draconic::core::Quat Rotation() const {
        return draconic::core::Quat::FromAxisAngle(draconic::core::Vec3{ 0.0f, 1.0f, 0.0f }, yaw)
             * draconic::core::Quat::FromAxisAngle(draconic::core::Vec3{ 1.0f, 0.0f, 0.0f }, pitch);
    }
    [[nodiscard]] draconic::core::Vec3 Forward() const {
        return draconic::core::RotateVector(Rotation(), draconic::core::Vec3{ 0.0f, 0.0f, -1.0f });
    }
    [[nodiscard]] draconic::core::Vec3 Right() const {
        return draconic::core::RotateVector(Rotation(), draconic::core::Vec3{ 1.0f, 0.0f, 0.0f });
    }

    // Apply this frame's input. Mouse: RMB (or Tab-capture) = free look; Alt+LMB = turntable orbit
    // about the focus point (Maya-style); MMB = pan; wheel = dolly/zoom. Plus WASD/QE move + Shift fast.
    void Update(draconic::runtime::IApplicationHost& host, draconic::core::f32 dt) {
        namespace rt = draconic::runtime;
        using draconic::core::Vec3;
        auto* input = (host.Platform() != nullptr) ? host.Platform()->Input() : nullptr;
        rt::IKeyboard* kb    = (input != nullptr) ? input->Keyboard() : nullptr;
        rt::IMouse*    mouse = (input != nullptr) ? input->Mouse() : nullptr;
        if (kb == nullptr) { return; }

        if (mouse != nullptr) {
            if (kb->IsKeyPressed(rt::KeyCode::Tab)) {
                mouseCaptured = !mouseCaptured;
                mouse->SetRelativeMode(mouseCaptured);
                mouse->SetCursorVisible(!mouseCaptured);
            }
            const bool alt = kb->IsKeyDown(rt::KeyCode::LeftAlt) || kb->IsKeyDown(rt::KeyCode::RightAlt);

            if (alt && mouse->IsButtonDown(rt::MouseButton::Left)) {
                // Turntable orbit: rotate about the focus point ahead, keeping it fixed.
                const Vec3 focus = position + Forward() * focusDistance;
                yaw   -= mouse->DeltaX() * lookSensitivity;
                pitch -= mouse->DeltaY() * lookSensitivity;
                pitch  = draconic::core::Clamp(pitch, -1.55f, 1.55f);
                position = focus - Forward() * focusDistance;
            } else if (mouseCaptured || mouse->IsButtonDown(rt::MouseButton::Right)) {
                // Free look (rotate in place).
                yaw   -= mouse->DeltaX() * lookSensitivity;
                pitch -= mouse->DeltaY() * lookSensitivity;
                pitch  = draconic::core::Clamp(pitch, -1.55f, 1.55f);
            }

            // MMB pan: drag moves the view laterally (content follows the cursor). Scaled by the focus
            // distance so the pan feels consistent regardless of zoom.
            if (mouse->IsButtonDown(rt::MouseButton::Middle)) {
                const draconic::core::f32 s = panSensitivity * focusDistance;
                position = position - Right() * (mouse->DeltaX() * s) + Up() * (mouse->DeltaY() * s);
            }

            // Wheel dollies along the view forward (zoom) — scroll up = move in, down = move out — and
            // shrinks the orbit pivot distance so the turntable pivot tracks the zoom.
            const draconic::core::f32 scroll = mouse->ScrollY();
            if (scroll != 0.0f) {
                position = position + Forward() * (scroll * zoomSpeed);
                focusDistance = draconic::core::Max(1.0f, focusDistance - scroll * zoomSpeed);
            }
        }

        const Vec3 fwd   = Forward();
        const Vec3 right = Right();
        const draconic::core::f32 speed = (kb->IsKeyDown(rt::KeyCode::LeftShift) ? fastSpeed : moveSpeed) * dt;
        Vec3 move{ 0.0f, 0.0f, 0.0f };
        if (kb->IsKeyDown(rt::KeyCode::W)) { move = move + fwd; }
        if (kb->IsKeyDown(rt::KeyCode::S)) { move = move - fwd; }
        if (kb->IsKeyDown(rt::KeyCode::D)) { move = move + right; }
        if (kb->IsKeyDown(rt::KeyCode::A)) { move = move - right; }
        if (kb->IsKeyDown(rt::KeyCode::E)) { move = move + Vec3{ 0.0f, 1.0f, 0.0f }; }
        if (kb->IsKeyDown(rt::KeyCode::Q)) { move = move - Vec3{ 0.0f, 1.0f, 0.0f }; }
        if (draconic::core::Dot(move, move) > 0.0f) { position = position + draconic::core::Normalized(move) * speed; }
    }
};

} // namespace draconic::samples
