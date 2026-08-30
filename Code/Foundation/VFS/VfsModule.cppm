// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::VFS - the `foundation.vfs` module.
//
// The virtual filesystem: byte access addressed by logical path, decoupled from
// any specific backend (disk, archive, memory, ...). Sits between Core (stream
// primitives) and higher layers (content database, resources). One named module
// composed of partitions, re-exported here.

export module foundation.vfs;

export import :ifilesystem;
export import :source_path;
export import :native_filesystem;
export import :vfs;
export import :data_root;
