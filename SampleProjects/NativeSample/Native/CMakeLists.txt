# NativeSample - the reference native game module (game-native-code.md).
#
# A game's native code is an engine-style module: util_add_engine_library builds it as
# the dev .so in shared-engine builds (the editor/player dlopen it via the manifest's
# nativeModule) and as a static library in ship builds (the exporter links it into
# Engine.GamePlayer). One source, both worlds.
util_add_engine_library(NativeSample ALIAS Game::NativeSample)
target_sources(NativeSample PRIVATE NativeSamplePlugin.cpp)
target_link_libraries(NativeSample PRIVATE Foundation::Runtime Foundation::Core Foundation::Policy)
# The dev .so lands HERE - exactly where the manifest's nativeModule points - so the
# editor/player dlopen what was just built. Ship (static) archives are unaffected.
set_target_properties(NativeSample PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
