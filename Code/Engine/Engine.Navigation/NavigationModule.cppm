// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::Navigation - the `engine.navigation` module.
//
// Scene integration for the navmesh core: the zone + agent
// components and the per-scene NavigationSceneSystem that loads baked zones, runs a Detour crowd
// per zone, and steers agents to targets. Bake-source collection + editor bake action live in
// Editor.Navigation; the script facade surfacing lives out-of-tree.

export module engine.navigation;

export import :components;
export import :subsystem;
