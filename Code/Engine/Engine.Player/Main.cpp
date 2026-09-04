// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine.Player - the dev/generic desktop entry point: the whole boot lives in
// engine.player.main (PlayerMain.cpp, launcher-as-library - game-native-code.md N1);
// a SHIP build replaces this stub with a generated one passing the game's
// statically-linked native plugin.

import engine.player.main;

int main(int argc, char** argv)
{
    return engine::player::PlayerMain(argc, argv, nullptr);
}
