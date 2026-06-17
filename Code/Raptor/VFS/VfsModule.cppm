// Raptor::VFS — the `raptor.vfs` module.
//
// The virtual filesystem: byte access addressed by logical path, decoupled from
// any specific backend (disk, archive, memory, ...). Sits between Core (stream
// primitives) and higher layers (content database, resources). One named module
// composed of partitions, re-exported here.

export module raptor.vfs;

export import :ifilesystem;
export import :native_filesystem;
export import :vfs;
