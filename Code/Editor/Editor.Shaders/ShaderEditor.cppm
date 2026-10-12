// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Shaders - the `editor.shaders` module.
//
// The editor side of custom shaders (Specs/custom-shaders.md, phase 3): the HLSL syntax tables
// CodeEditView highlights a shader with, registered into the toolkit's CodeLexerRegistry under
// "hlsl" and "hlsli".

module;
#include "Core/Prelude.h"

export module editor.shaders;

import foundation.core;

export namespace editor
{
    /// Registers the HLSL lexer under "hlsl" and "hlsli"; each lexer it makes comes from
    /// `allocator`, which must outlive the registry's use (the editor's root allocator).
    void RegisterHlslEditorUI(foundation::core::IAllocator& allocator);
}
