// Raptor Core — :containers aggregator partition.
//
// Re-exports the individual container partitions so consumers keep using a
// single `import` (CoreModule re-exports this). Each container lives in its own
// file/partition: Span, Array, RingBuffer, IntrusiveList.

export module raptor.core:containers;

export import :span;
export import :array;
export import :ring_buffer;
export import :intrusive_list;
