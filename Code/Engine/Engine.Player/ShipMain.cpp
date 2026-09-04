// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine.GamePlayer - the SHIP entry point (game-native-code.md N3): the same launcher
// the dev player uses, with the project's native game plugin STATICALLY linked. One
// checked-in stub covers every game: the plugin contract already exports
// extern "C" CreatePlugin (the exact symbol PluginHost resolves via dlopen in dev),
// so referencing it here pulls the game's registration out of its static archive -
// no generated main, no forced-reference chains, no whole-archive.

import engine.player.main;
import foundation.runtime;

extern "C" foundation::runtime::IRuntimePlugin* CreatePlugin();

int main(int argc, char** argv)
{
    return engine::player::PlayerMain(argc, argv, CreatePlugin());
}
