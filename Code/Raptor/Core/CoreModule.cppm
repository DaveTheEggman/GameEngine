// Raptor Core — primary module interface unit
//
// `raptor.core` is one named module composed of partitions (one per subsystem).
// This unit re-exports them so consumers write a single `import raptor.core;`.
// As subsystems land, add an `export import :partition;` line here.

export module raptor.core;

export import :base;
export import :memory;
export import :smart_ptr;
// export import :containers;
// ...
