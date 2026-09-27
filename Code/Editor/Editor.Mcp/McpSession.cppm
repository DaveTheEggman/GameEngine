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
import foundation.resource;
import editor.project;

using namespace foundation::core;
using foundation::json::JsonValue;
namespace content = foundation::content;

export namespace editor::mcp
{
    // The project every tool works on (null until one is open). NON-OWNING: the editor host
    // points it at the editor's live project - one ContentDatabase, one writer - and the stdio
    // host at the project it opened and keeps in its ProjectOwner. The pointee must outlive the
    // server the tools are registered on.
    struct ProjectSession
    {
        editor::EditorProject* project = nullptr;
        /// A tool wrote a source asset (scene_write / prefab_write, created or overwritten): the
        /// host reacts as it does to any change made outside a page - the editor host tells
        /// the open pages editing it (EditorContext::NotifyAssetExternallyModified); the stdio
        /// host has no pages and leaves it unset.
        Function<void(const Guid&)> onAssetWritten;
        /// The host's resource manager with its factories, when it has one (the editor's, over
        /// the project's cooked DB). NON-OWNING and optional: the scene format reference joins a
        /// component's Ref<T> to the asset type an agent authors through the factories
        /// (scene-format-reference.md, D4); a host without one serves the resource name alone.
        const foundation::resource::ResourceManager* resources = nullptr;
    };

    // The stdio host's project ownership: project_open stores what it opened here and points
    // the session at it. The editor host has no owner - its project is the editor's own.
    struct ProjectOwner
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
