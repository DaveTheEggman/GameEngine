// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Profiler - `foundation.profiler` (Specs/profiler.md): the capture store every thread
/// records into (:store), the sources a viewer reads it through (:source: the live store or a
/// capture), and the stream a capture file is (:stream).

export module foundation.profiler;

export import :store;
export import :source;
export import :stream;
