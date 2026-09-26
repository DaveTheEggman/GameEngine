// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Scene - :mcp_tools partition.
//
// The scene editor's MCP tools - what only a live scene page can serve, registered as the
// scene editor's contribution to the editor's MCP host (EditorContext::RegisterMcpToolContribution
// from RegisterSceneEditor): selection_get / selection_set over a page's entity selection, and
// simulate_start / simulate_stop over its edit-mode Simulate, and entity_inspect over an entity's
// reflected components. Every tool is PAGE-ADDRESSED
// (`page` = the scene or prefab asset's guid, as page_list reports it), defaulting to the active
// page when that is a scene page, and every result names the page it acted on - several scene
// pages may be open, each with its own selection. The page is reached through the interface it
// publishes (EditorPage::Service<ISceneEditorPage>), and the selection through its edit context,
// so the hierarchy, the inspector and the gizmos follow an agent's selection exactly as a click.
module;
#include "Core/Prelude.h"

export module editor.scene:mcp_tools;

import foundation.core;
import foundation.mcp;
import editor.core;

using namespace foundation::core;

export namespace editor
{
    /// The number of tools RegisterSceneLiveTools registers (selection_get / selection_set /
    /// simulate_start / simulate_stop / entity_inspect); a tripwire like kEngineToolCount.
    inline constexpr usize kSceneLiveToolCount = 5;

    void RegisterSceneLiveTools(foundation::mcp::McpServer& server, EditorContext& context);
}
