// Raptor Core — primary module interface unit
//
// `raptor.core` is one named module composed of partitions (one per subsystem).
// This unit re-exports them so consumers write a single `import raptor.core;`.
// As subsystems land, add an `export import :partition;` line here.

export module raptor.core;

export import :base;
export import :memory;
export import :linear_allocator;
export import :stack_allocator;
export import :pool_allocator;
export import :frame_allocator;
export import :tracking_allocator;
export import :memory_tag;
export import :ref_counted;
export import :unique_ptr;
export import :system;
export import :span;
export import :array;
export import :ring_buffer;
export import :intrusive_list;
export import :string;
export import :hash;
export import :hash_map;
export import :hash_set;
export import :format;
export import :log;
export import :math;
export import :color;
export import :random;
export import :matrix;
export import :geometry;
export import :type_info;
export import :type_registry;
export import :object;
export import :variant;
export import :instance;
export import :reflection;
export import :enum_reflection;
export import :io;
export import :path;
export import :filesystem;
export import :ifilesystem;
export import :native_filesystem;
export import :vfs;
export import :iserializer;
export import :serializer;
export import :binary_serializer;
export import :serialize;
export import :atomic;
export import :thread;
export import :mutex;
export import :scoped_lock;
export import :spin_lock;
export import :condition_variable;
export import :semaphore;
export import :shared_mutex;
export import :job_system;
export import :library;
