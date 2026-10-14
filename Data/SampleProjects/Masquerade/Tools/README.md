# Masquerade's tools

The scripts that build Masquerade through the editor's MCP tools. They talk to a running editor
with the project open and its MCP server on (`Tools.Editor Data/SampleProjects/Masquerade
--mcp-port 7405`; `mcp.py` reads the token the editor writes). Each script's own header says what
it makes, what it needs first and how to call it. Generated files land in `generated/`
(gitignored); the imported copies live in the project's `Sources/`.

## In order

1. **Models** (Blender 5, headless; the shared modelling kit is `blender/kit3d.py`):
   - `blender/palace.py`: the palace kit on its 2 m grid (flagstones, marble, wall, window, pier,
     column, balustrade, fountain, lantern, planter, bench, chandelier, the orchestra's dais), into
     `generated/Palace`.
   - `blender/guest.py`: the guests, six costumes (three gowns, three coats) on the one rig of
     `blender/figure.py`, each with the clips Idle, Walk, Talk and Dance, into `generated/Guests`.
   - `importmodels.py`: imports them, the kit into `Models/Palace` and the guests into
     `Models/Guests`, a group per model.
2. **Project setup**: `controls.py` (the input map, made the project's default) and `look.py` (the
   night environment and the post profile every map shares, written when a map is).
3. **Scripts**: `importscripts.py` imports `scripts/*.as`: a guest (`Guest`: wandering, talking
   and dancing to the cues), the player (`Player`), the orchestra and its cues (`Orchestra`), and
   the camera (`CameraRig`).
4. **The maps**: `maps.py` writes each map's scene (the Palace for now): the ground, walls and
   props with their colliders, the lights, the places guests go, the orchestra, 150 guests drawn
   as instances of their costumes, the player and the camera; it bakes the map's navigation and
   makes the Palace the project's default scene.

`scenegen.py` (the scene document writer) and `mcp.py` (the MCP client) are the helpers the others
share.
