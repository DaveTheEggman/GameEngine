// Draconic UI - primary module interface unit for `draconic.ui`.
//
// The retained-mode UI framework, a faithful port of Sedulous.UI onto the Draconic
// VG/Fonts stack. Platform-agnostic: the core knows nothing about the shell/input -
// a separate draconic.ui.shell bridge feeds its abstract input/clipboard seams.
//
// One named module composed of partitions (one per subsystem), re-exported here so
// consumers write a single `import draconic.ui;`. Add `export import :partition;`
// lines as the port progresses (bottom-up: Core -> Layout -> Drawing -> ...).

export module draconic.ui;

export import :enums;
export import :thickness;
export import :view_transform;
export import :view_id;
export import :event;
export import :property_owner;
export import :property;
export import :icommand;
export import :iclipboard;
export import :debug_settings;
export import :control_state;
export import :draw_context;
export import :drawable;
export import :color_drawable;
export import :rounded_rect_drawable;
export import :shape_drawable;
export import :gradient_drawable;
export import :inset_drawable;
export import :layer_drawable;
export import :state_list_drawable;
export import :image_drawable;
export import :nine_slice_drawable;
export import :atlas_image_drawable;
export import :atlas_nine_slice_drawable;
export import :svg_drawable;
export import :unit;
export import :size_spec;
export import :box_constraints;
export import :gravity;
export import :gravity_helper;
export import :layout_params;
export import :view;
export import :style_property;
export import :style_value;
export import :style_selector;
export import :style_rule;
export import :style_sheet;
export import :palette;
