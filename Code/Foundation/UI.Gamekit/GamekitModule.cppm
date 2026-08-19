// UI.Gamekit - `foundation.ui.gamekit` module aggregator.
//
// Game-UI conveniences over CORE foundation.ui, sibling of foundation.ui.toolkit: a UIScreen page view
// (3 input modes + declared transition + default focus) and a ScreenStack (push/pop/replace over a
// RootView, with focus save/restore, modal input shielding, and built-in tween transitions). Consumers
// write `import foundation.ui.gamekit;`.

export module foundation.ui.gamekit;

export import :screen;
export import :stack;
