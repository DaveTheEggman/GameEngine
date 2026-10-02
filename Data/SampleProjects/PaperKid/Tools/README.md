# PaperKid's authoring tools

PaperKid is built through the engine editor's MCP tools (open the project with `Tools.Editor
<this project> --mcp-port 7405`, the port `mcp.py` uses unless `MCP_PORT` says otherwise). These
scripts drive them; none of this is game content.

- `mcp.py`: a small MCP client (`python3 mcp.py http <tool> '<json args>'`, or `--list`).
- `pkgen.py`: scene and prefab XML built from the running editor's `component_schema`, so a
  component record starts from the engine's own defaults.
- `kit.py`: the blockout kit's prefabs (houses, road, kerb, car, pedestrian, junk, newspaper,
  delivery zone); writes `kit.json` (prefab name -> guid).
- `block.py`: the five blocks (one generator, `town()`, sized by the ring road's distance from
  the middle; the `BLOCKS` table is the difficulty ramp) and the Start scene. `block.py Block3`
  writes just that one. Bake a block's navigation after writing it (`navigation_bake`).
- `scripts/*.as` + `render.py`: the game's scripts with `{{AssetName}}` placeholders;
  `render.py <Name>...` fills in the asset ids, writes `Sources/<Name>.as` and compile-checks it.
- `anim.py`: the property-animation clips (the marker's bob, the porch mat's pulse) and the
  throw guides' glowing unlit material, written through `asset_data_write`; `kit.py` puts them
  on the delivery zone's pieces and the AimDot and TargetRing prefabs.
- `drive.py`: a closed-loop playtest of Block1 over `pie_run` (laps the ring, throws at each
  zone once).
