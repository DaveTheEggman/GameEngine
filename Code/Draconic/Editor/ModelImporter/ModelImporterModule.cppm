/// Draconic::ModelImporter - the `draconic.modelimporter` umbrella module.
///
/// The top-level model import pipeline: it converts a loaded `draconic.model` Model
/// (the loader IR) into the engine's cooked *Source types and cooks them through the
/// editor stack into a content database. Faithful-but-fit-for-Draconic: import is
/// hierarchy-preserving (node -> entity), not Sedulous's unconditional mesh merge.
export module draconic.modelimporter;

export import draconic.model.resource; // the cooked-model runtime types (moved out of tooling)
export import :mesh_convert;
export import :anim_convert;
export import :cook;
export import :file_import;
export import :load;
