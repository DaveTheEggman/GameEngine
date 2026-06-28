#pragma once
// Shared free-fly camera for the dev samples: WASD/QE move, hold RMB (or Tab to capture) to look,
// Shift to move fast. Provides position + orientation; the sample consumes it as it likes (set a
// camera-entity transform, or build a ViewCamera). The including translation unit must have
// imported raptor.core + raptor.runtime.platform + raptor.runtime.client (this header uses their
// types via fully-qualified names).

namespace raptor::samples {

struct FlyCamera {
    raptor::core::Vec3 position{ 0.0f, 14.0f, 30.0f };
    raptor::core::f32  yaw   = 0.0f;       // 0 => looking down -Z
    raptor::core::f32  pitch = -0.3f;      // tilt down a touch
    bool               mouseCaptured = false;
    raptor::core::f32  moveSpeed = 50.0f, fastSpeed = 200.0f, lookSensitivity = 0.003f;
    raptor::core::f32  zoomSpeed = 3.0f;   // world units per wheel notch (dolly along forward)

    // Orientation as a quaternion (yaw about world Y, then pitch about local X). Default forward -Z.
    [[nodiscard]] raptor::core::Quat Rotation() const {
        return raptor::core::Quat::FromAxisAngle(raptor::core::Vec3{ 0.0f, 1.0f, 0.0f }, yaw)
             * raptor::core::Quat::FromAxisAngle(raptor::core::Vec3{ 1.0f, 0.0f, 0.0f }, pitch);
    }
    [[nodiscard]] raptor::core::Vec3 Forward() const {
        return raptor::core::RotateVector(Rotation(), raptor::core::Vec3{ 0.0f, 0.0f, -1.0f });
    }
    [[nodiscard]] raptor::core::Vec3 Right() const {
        return raptor::core::RotateVector(Rotation(), raptor::core::Vec3{ 1.0f, 0.0f, 0.0f });
    }

    // Apply this frame's input: mouse look (RMB held or Tab-captured) + WASD/QE move + Shift fast.
    void Update(raptor::runtime::IApplicationHost& host, raptor::core::f32 dt) {
        namespace rt = raptor::runtime;
        using raptor::core::Vec3;
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
            if (mouseCaptured || mouse->IsButtonDown(rt::MouseButton::Right)) {
                yaw   -= mouse->DeltaX() * lookSensitivity;
                pitch -= mouse->DeltaY() * lookSensitivity;
                pitch  = raptor::core::Clamp(pitch, -1.55f, 1.55f);
            }
            // Wheel dollies along the view forward (zoom) — scroll up = move in, down = move out.
            const raptor::core::f32 scroll = mouse->ScrollY();
            if (scroll != 0.0f) { position = position + Forward() * (scroll * zoomSpeed); }
        }

        const Vec3 fwd   = Forward();
        const Vec3 right = Right();
        const raptor::core::f32 speed = (kb->IsKeyDown(rt::KeyCode::LeftShift) ? fastSpeed : moveSpeed) * dt;
        Vec3 move{ 0.0f, 0.0f, 0.0f };
        if (kb->IsKeyDown(rt::KeyCode::W)) { move = move + fwd; }
        if (kb->IsKeyDown(rt::KeyCode::S)) { move = move - fwd; }
        if (kb->IsKeyDown(rt::KeyCode::D)) { move = move + right; }
        if (kb->IsKeyDown(rt::KeyCode::A)) { move = move - right; }
        if (kb->IsKeyDown(rt::KeyCode::E)) { move = move + Vec3{ 0.0f, 1.0f, 0.0f }; }
        if (kb->IsKeyDown(rt::KeyCode::Q)) { move = move - Vec3{ 0.0f, 1.0f, 0.0f }; }
        if (raptor::core::Dot(move, move) > 0.0f) { position = position + raptor::core::Normalized(move) * speed; }
    }
};

} // namespace raptor::samples
