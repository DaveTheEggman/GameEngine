// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Scene - :tools implementation (SelectTransformTool).

module;
#include "Core/Prelude.h"

module editor.scene;

import foundation.core;
import foundation.scene;
import foundation.shell;
import editor.viewporttools;

using namespace foundation::core;

namespace editor
{
    namespace scene = foundation::scene;

    bool SelectTransformTool::Update(const ViewportToolInput& input)
    {
        GizmoFrameInput gizmoInput;
        gizmoInput.ray = GizmoRay{input.ray.origin, input.ray.direction};
        gizmoInput.cameraPosition = input.cameraPosition;
        gizmoInput.cameraForward = input.cameraForward;
        gizmoInput.fovY = input.fovY;
        // editingLocked (Simulate) maps to the controller's pointer-less update: the gizmo pose
        // keeps tracking what physics/animation move, but no hover, no drags, no mode hotkeys -
        // exactly the pre-framework Simulate branch.
        gizmoInput.pointerValid = input.pointerValid && !input.editingLocked;
        gizmoInput.leftPressed = input.leftPressed;
        gizmoInput.leftDown = input.leftDown;
        gizmoInput.leftReleased = input.leftReleased;
        gizmoInput.snap = input.ctrl;
        if (foundation::shell::IKeyboard* keyboard = input.keyboard;
            keyboard != nullptr && gizmoInput.pointerValid)
        {
            gizmoInput.keyTranslate = keyboard->IsKeyPressed(foundation::shell::KeyCode::W);
            gizmoInput.keyRotate = keyboard->IsKeyPressed(foundation::shell::KeyCode::E);
            gizmoInput.keyScale = keyboard->IsKeyPressed(foundation::shell::KeyCode::R);
            gizmoInput.keyToggleSpace = keyboard->IsKeyPressed(foundation::shell::KeyCode::X);
        }
        const bool consumed = m_gizmos.Update(gizmoInput);

        PollPick(); // a GPU answer from an earlier click lands here
        if (!consumed && input.pointerOver && input.leftPressed)
        {
            PickOnClick(input);
        }
        return consumed;
    }

    void SelectTransformTool::OnDeactivate()
    {
        // Gesture-end guarantee: a pointer-less update finishes/aborts any in-flight drag so no
        // half-applied command group survives a tool switch.
        GizmoFrameInput gizmoInput;
        gizmoInput.pointerValid = false;
        (void)m_gizmos.Update(gizmoInput);
    }

    void SelectTransformTool::PickOnClick(const ViewportToolInput& input)
    {
        const Guid cpu = CpuPick(input);
        const bool pixelValid = input.viewportWidth > 0 && input.viewportHeight > 0 &&
                                input.pointerX >= 0 && input.pointerY >= 0 &&
                                static_cast<u32>(input.pointerX) < input.viewportWidth &&
                                static_cast<u32>(input.pointerY) < input.viewportHeight;
        if (m_picker != nullptr && pixelValid)
        {
            const u32 request = m_picker->RequestPick(input.pointerX, input.pointerY, 1, 1);
            if (request != 0)
            {
                // The newest click wins: an older answer still in flight is ignored when it lands.
                m_pendingPick = request;
                m_pendingCpuPick = cpu;
                m_pendingCtrl = input.ctrl;
                return;
            }
        }
        ApplyPick(cpu, input.ctrl);
    }

    void SelectTransformTool::PollPick()
    {
        if (m_pendingPick == 0 || m_picker == nullptr)
        {
            return;
        }
        Array<scene::EntityHandle> hits;
        if (!m_picker->TryTakePick(m_pendingPick, hits))
        {
            return;
        }
        m_pendingPick = 0;
        scene::Scene& scene = m_edit->Scene();
        Guid picked = m_pendingCpuPick; // the GPU saw nothing drawn there: the CPU answer stands
        for (const scene::EntityHandle& h : hits)
        {
            if (scene.IsValid(h))
            {
                picked = scene.GetEntityId(h);
                break;
            }
        }
        ApplyPick(picked, m_pendingCtrl);
    }

    void SelectTransformTool::ApplyPick(const Guid& picked, bool ctrl)
    {
        Selection<Guid>& selection = m_edit->EntitySelection();
        if (picked != Guid{})
        {
            if (ctrl)
            {
                selection.Toggle(picked);
            }
            else
            {
                selection.Set(picked);
            }
        }
        else if (!ctrl)
        {
            selection.Clear();
        }
    }

    Guid SelectTransformTool::CpuPick(const ViewportToolInput& input) const
    {
        scene::Scene& scene = m_edit->Scene();
        const Float3 origin = input.ray.origin;
        const Float3 dir = input.ray.direction;

        Guid best;
        f32 bestT = kFloatMax;
        scene.ForEachEntity(
            [&](scene::EntityHandle e)
            {
                const Float4x4 world = scene.GetWorldMatrix(e);
                const Float3 p{world.m[3][0], world.m[3][1], world.m[3][2]};
                const Float3 toCenter = p - origin;
                const f32 t = Dot(toCenter, dir);
                if (t <= 0.0f || t >= bestT)
                {
                    return;
                }
                const Float3 closest = origin + dir * t;
                const Float3 d = p - closest;
                // Screen-constant-ish pick radius: grows with distance, floors for close-ups.
                const f32 radius = Max(0.15f, t * 0.02f);
                if (Dot(d, d) <= radius * radius)
                {
                    bestT = t;
                    best = scene.GetEntityId(e);
                }
            });
        return best;
    }
}
