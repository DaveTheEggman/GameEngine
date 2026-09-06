// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Single translation unit for the stb_truetype implementation.
// Module units cannot define this macro (static symbol collisions), so the
// implementation lives in a regular .cpp linked into Foundation::Fonts.TrueType.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
