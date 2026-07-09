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
