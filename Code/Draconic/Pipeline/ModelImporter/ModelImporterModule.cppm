/// Draconic::ModelImporter - the `modelimporter` umbrella module.
///
/// The top-level model import pipeline: it converts a loaded `foundation.model` Model
/// (the loader IR) into the engine's cooked *Source types and cooks them through the
/// editor stack into a content database. Faithful-but-fit-for-Draconic: import is
/// hierarchy-preserving (node -> entity), not Sedulous's unconditional mesh merge.
export module modelimporter;

export import foundation.model.resource; // the cooked-model runtime types (moved out of tooling)
export import :mesh_convert;
export import :anim_convert;
export import :cook;
export import :file_import;
export import :load;
