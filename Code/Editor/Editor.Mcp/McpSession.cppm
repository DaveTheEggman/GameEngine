// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Mcp - :session partition
//
// The shared host-session state + small JSON/content helpers every editor.mcp tool partition
// uses (extracted from ProjectTools when scene_tools became a second partition - partitions
// cannot see the primary unit's declarations).

module;
#include "Core/Prelude.h"

export module editor.mcp:session;

import foundation.core;
import foundation.json;
import foundation.content;
import editor.core;

using namespace foundation::core;
using foundation::json::JsonValue;
namespace content = foundation::content;

export namespace editor::mcp
{
    // The MCP host's current project (null until project_open succeeds). Owned by the host; must
    // outlive the server the tools are registered on.
    struct ProjectSession
    {
        UniquePtr<editor::EditorProject> project;
    };
}

export namespace editor::mcp::detail
{
    // Walk (creating as needed) a slash-joined group path under `root`, returning the leaf group.
    // Empty path returns root. Used to place a created asset in a chosen source-DB group.
    inline content::Group* ResolveGroupPath(content::Group* root, StringView path)
    {
        content::Group* group = root;
        usize start = 0;
        for (usize i = 0; i <= path.Size(); ++i)
        {
            const bool atEnd = (i == path.Size());
            if (!atEnd && path[i] != utf8char('/'))
            {
                continue;
            }
            const StringView part = path.SubStr(start, i - start);
            if (!part.IsEmpty())
            {
                group = group->CreateGroup(part);
            }
            start = i + 1;
        }
        return group;
    }

    // A Guid as its canonical 36-char string.
    inline JsonValue GuidToJson(const Guid& id)
    {
        utf8char buffer[37];
        id.ToChars(buffer);
        return JsonValue::MakeString(String(buffer));
    }
}
