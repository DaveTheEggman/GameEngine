// Raptor::RenderGraph — the `raptor.rendergraph` module.
//
// A render graph over the RHI: passes declare resource accesses, the graph
// resolves dependencies, allocates/aliases transient resources, and inserts the
// right barriers automatically. Ported from Sedulous.RenderGraph (whose RHI is
// the same one Raptor's RHI is a faithful port of). One named module composed of
// partitions, re-exported here.

export module raptor.rendergraph;

export import :types;
export import :callbacks;
export import :descriptors;
export import :persistent_resource;
export import :resource;
export import :pass;
export import :state_tracker;
export import :barrier_solver;
