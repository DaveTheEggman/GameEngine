# PaperKid

A small arcade paper-route game, built entirely through the engine's MCP tools: the second test,
after Sky Hopper, of how far an agent gets making a game with them, where every missing or wrong
tool or engine behaviour was fixed in the engine as part of the work. It replaces an earlier
PaperKid that stopped after one block, and follows its spec
(`Documentation/Specs/paperkid.md`).

![PaperKid, riding a block](../../../Documentation/Images/PaperKid-Play.png)

## The game

Ride a bike round a town block with a stack of papers and a countdown, and throw papers onto the
porches of the subscriber houses before time runs out. A soft auto-aim leans each throw toward
the nearest subscriber ahead. Cars and pedestrians move around the block on the navmesh, and bins,
hydrants and cones stand on the verges: hit any of them and you crash, losing speed and a few
seconds. Meet the block's quota to clear it; run out of time, or of papers, and you lose a life.

Five blocks on a difficulty ramp, from a 48 m ring road to an 80 m one, with more subscribers, a
higher quota, busier and faster traffic and less time for the distance. Three lives; a failed
block replays from the score you had when it started. A delivery scores 100 and a clear adds 5
for every second left. The HUD shows the time, deliveries, papers, score and lives, and a
minimap: a top-down render texture of the block with the subscribers (dimmed once delivered) and
the bike, turned to its heading. A title screen with settings (master, music and effect
volumes), a pause menu, block cleared and failed screens, and a Game over or route complete
summary.

The feel: chiptune music on the menus and across the blocks, a sound for every throw, delivery
and crash, a jingle when a block is cleared or failed and a voice over the final screen, and a
ticking clock in the last ten seconds ("Hurry up!" at twenty). The markers over the subscribers
bob and their porch mats breathe, the bike leans into turns and wobbles after a crash, the camera
shakes on a crash, and a delivery's points rise from the score. Near a subscriber, a throw is
shown before it is made: a trail of glowing dots along the paper's path and a ring spinning on the
porch it is pulled toward.

Controls: WASD to ride and steer, Space to throw, Escape to pause; the arrows and Enter drive the
menus. A gamepad works throughout: the left stick to ride and steer, A (cross) to throw and
confirm, Start (Options) to pause.

## Project layout

- `Project.xml`: the manifest: the start scene, the Game script, the input map, the fonts and
  the render resolution (1280x720, letterboxed).
- `Sources/`: the raw sources: the AngelScript scripts, the UI markup, the fonts, the sound
  effects and the music.
- `Content/`: the asset envelopes (`*.xasset`) and their sidecars: the five blocks and the title
  scene, the blockout kit's prefabs, each block's baked navmesh, the minimap's render texture, the
  audio clips and the markers' animation clips.
- `Tools/`: the authoring scripts that drove the editor's MCP tools to build the game (the kit,
  the block generator, the scripts with their asset ids, a closed-loop playtest). Not game
  content; `Tools/README.md` says what each does.
- `export_presets.xml`: the Linux desktop export target.
- `CREDITS.md` and `Licenses/`: the music (Juhani Junkala, CC0), the sound effects (Kenney, CC0)
  and the two fonts (OFL and Apache 2.0). The art is a primitive blockout made in the project.
- `Cooked/`, `.cache/`, `Editor/`, `Dist/`: generated, and gitignored; the tools rebuild them.

Open the project from the editor's project manager.
