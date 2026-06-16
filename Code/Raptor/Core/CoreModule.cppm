// Raptor Core — primary module interface unit
//
// `raptor.core` is one named module composed of partitions (one per subsystem).
// This unit re-exports them so consumers write a single `import raptor.core;`.
// As subsystems land, add an `export import :partition;` line here.

export module raptor.core;

export import :base;
export import :memory;
export import :smart_ptr;
export import :system;
export import :containers;
export import :string;
export import :hash;
export import :hash_map;
export import :format;
export import :log;
export import :math;
export import :matrix;
export import :geometry;
export import :rtti;
export import :variant;
export import :io;
export import :path;
export import :serialization;
export import :threading;
export import :library;
