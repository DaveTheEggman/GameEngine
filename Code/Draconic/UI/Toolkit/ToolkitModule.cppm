// Draconic UI Toolkit - primary module interface unit for `draconic.ui.toolkit`.
//
// The editor-grade widget set (a faithful port of Sedulous.UI.Toolkit) layered on draconic.ui: docking,
// property grid, node graph, color/gradient/curve editors, and the menu/tool/status bars. One named
// module composed of partitions (one per control), re-exported here so consumers write a single
// `import draconic.ui.toolkit;`. Add `export import :partition;` lines as the port progresses (bottom-up:
// simple bars -> property grid -> pickers -> curves -> docking -> node graph -> theme extension).

export module draconic.ui.toolkit;

export import :status_bar;
export import :toolbar;
export import :menu_bar;
export import :split_view;
export import :breadcrumb_bar;
