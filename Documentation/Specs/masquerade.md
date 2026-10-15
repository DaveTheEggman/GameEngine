# Masquerade - the fifth sample, social stealth in a crowd over the network

> STATUS: APPROVED 2026-10-14. P0 (network commands for scripts), P1 (replicated state) and P2
> (the crowd's engine features) and P3 (the ball, solo: three maps, the round, bots) BUILT
> 2026-10-14; P4 (the ball over the network) built and played in two Game tabs 2026-10-14, two
> machines and a browser to try. Agreed (user 2026-10-14): the masked
> ball idea; AngelScript (the Luau sample is out of 0.1); an authoritative host that plays, with
> players joining it natively or from a browser through the host's WebSocket gateway (a browser
> never hosts, by design); `wss://` last, as the client's choice of `wss://` plus a documented TLS
> reverse proxy, with TLS in the engine only if wanted later. Up to 4 players and 150 guests.
> Three maps to start. Closes the 0.1 backlog's fifth sample (networking and crowds). Read CONVENTIONS.md
> first.

## What it is for

No sample uses the network stack or crowds yet, so neither has been shown or stressed in a game.
Here both are the game: the crowd is what the players hide in, and the network is what makes
hiding matter, since the other guests who move like guests are people.

## Concept

A masked ball at night: a palace courtyard with a fountain and lanterns, and a ballroom off it.
Some 150 guests in masks and gowns wander, gather in groups to talk, stop at the fountain, and
dance when the orchestra plays. Each player is one of them, and looks like one.

Each player has a target, another player, and is someone else's target. Find your target among
the guests and tag them before your hunter tags you. Move like a guest and you are hard to find;
run, walk against the flow, or stand still when the music calls everyone to dance, and you stand
out. Tag a guest by mistake and your mask flares, showing you to everyone for a few seconds.

## Maps

Three to start, each giving the crowd a different shape to hide in:

- **The Palace**: a courtyard with a fountain and lanterns, and a ballroom off it where the
  dancing is. Open ground outside, a dense floor inside.
- **The Canal**: a square on a canal at carnival, bridges and narrow quays where the crowd
  squeezes into lines, and gondolas passing under.
- **The Gardens**: a hedge maze and an orangery, guests strolling the paths in twos and threes,
  sight lines broken by hedges and glass.

The map is chosen before a round, by the host when playing over the network.

## Core loop (one round)

1. The round starts with every player placed among the guests; each sees who their target is (a
   portrait of the target's costume, which guests share, so it narrows the search, not ends it).
2. Players move at a guest's pace by default; a short sprint is allowed but marks you on your
   hunter's screen for a moment.
3. The orchestra's cues change what guests do: walk, gather, dance. A player who does not follow
   a cue stands out.
4. Tag: come close to a guest and press tag. Your target: you score, and they respawn elsewhere
   with a new costume. A guest: your mask flares (a reveal) and you lose points.
5. A round lasts a few minutes; the most points wins. Rounds repeat with new targets.

Solo, the other players are bots that play the same rules, so the web demo plays alone.

## Where it lives and how it is built

- **Project**: `Data/SampleProjects/Masquerade`, built and playtested through the MCP tools as the
  other samples, its authoring scripts in its `Tools/`: Blender scripts for the guests (one rig,
  several costumes and masks, the walk, idle, talk and dance clips), the level generator, the
  sounds.
- **Playtesting the network through the tools**: the editor runs several Game tabs at once; one
  hosts and another joins, so an agent plays both sides on one machine. Then two players for
  real: two native machines on a LAN, and a browser joining a native host.
- **Profiling**: the new profiler measures the crowd (P5's tab and `profiler_capture`), and the
  overlay shows it in the player.
- **Integration.Mcp** checks it reads at the current data versions and cooks, as the others.

## The engine it exercises

Networking (the authoritative host, replication, RPCs, late join, the WebSocket gateway), crowds
(navigation crowds, instanced skinning), custom shaders (masks, silk, the reveal), the profiler,
and the existing UI, audio and animation.

## Engine gaps known before starting

From a survey of the code (2026-10-14):

1. **Scripts cannot receive network messages** (`Net.on`, slice 1 of `Plans/networking-commands.md`,
   also in the 0.1 backlog): RPCs are send-only from scripts.
2. **Scripts cannot spawn networked entities** (`Net.spawn`, slice 2).
3. **No owning player and no input to the host** (slice 3): `NetworkComponent` has no owner peer,
   and nothing carries a client's input to the host.
4. **Join and leave reach C++ only**: scripts never hear a player arrive or go.
5. **Only transforms replicate**: no animation or game state (which clip a guest plays, a
   player's costume); a small replicated state component, or script properties marked
   replicated, is needed.
6. **Crowd size**: a navigation zone takes 128 agents at most (`kMaxAgentsPerZone`, hard-coded).
7. **Crowds and instanced skinning are not joined**: navigation agents are entities, instanced
   skinning draws one mesh at many transforms; nothing moves instances with agents. And one
   instanced crowd plays one clip at evenly spaced phases: a guest cannot switch between walk,
   idle and dance, and there is no script facade for it.
8. **`wss://`**: the browser client always builds a `ws://` URL.

## Phases

- **P0 Network commands for scripts** (gaps 1 to 4; `Plans/networking-commands.md` slices 1 to 3):
  `Net.on` with number and text payloads in both languages, `Net.spawn`, an owner peer and
  input to the host, join and leave events. Tests in Net and the script backends; the networking
  docs page's "What is missing" list shrinks.
  As built: `Net.on(name, fn(sender[, value]))`; `Net.spawn` and `Net.spawnFor(player, ...)`;
  player ids (0 the host, a client the id the session's welcome gives it), `Net.localPlayer`,
  `Net.isMine`, `NetworkComponent.owner`; `Net.rpcTo` (and number, text); `Net.onJoin` /
  `Net.onLeave`. Input to the host is an RPC whose sender the host checks; a leaver's guest is the
  game's to decide in `onLeave`. The plan's slice 4 (a cube demo) is this game instead.
- **P1 Replicated state** (gap 5): what a guest is doing and how a player looks, replicated and
  read by scripts on every peer. Tests: a host's change reaches a late joiner.
  As built (the user chose script-marked properties, 2026-10-14): a behaviour property marked
  `replicated` (AngelScript metadata word; Luau `Class.replicated = { ... }`) is kept the same on
  every peer, through a scene seam (`IReplicatedStateSource`) the script component implements.
  Clients run the same behaviours, so the game changes replicated values on the host only.
- **P2 Crowds at scale** (gaps 6 and 7), as general engine features with nothing crowd- or
  Masquerade-specific (user 2026-10-14). A survey found instanced skinning cannot be authored
  (raw borrowed skeleton and clip, a manager that is not saved; only native samples use it), the
  instanced mesh's pose policy and indices are not saved either, and nothing draws an entity as an
  instance. The layers:
  1. **Navigation**: the agents a zone takes is a field of the zone (today's 128 the default).
  2. **Render, pose policy**: `PoseAssignment` and `poseIndices` saved and editable (inspector,
     scripts); a `Unison` policy (every instance on a clip at the same phase: a synchronized
     crowd), beside Hashed, Sequential and Explicit.
  3. **Render, a clip per instance**: the pose pool laid out a block per clip; each instance's clip
     index picks the block and the policy picks the phase inside it. One clip and no indices draw
     as today.
  4. **Animation, authorable instanced skinning**: the skeleton and a list of clips as asset
     references (as SkeletalAnimationComponent), saved and inspected; the pool holds every clip at
     its phases. The native samples (AnimatedCrowd, WebScene, RenderStressTest) move to it.
  5. **Render, an entity drawn as an instance**: `InstanceMemberComponent` names an instanced
     mesh and a clip index; each frame extraction adds the members' world transforms and clips
     after the mesh's authored instances (never into its saved list). Anything moves a member:
     navigation, physics, replication, a script, the editor's gizmo. Script facade
     `InstanceMemberComponent.of(e).clip`, named like the other render components.
  Masquerade then uses them: a Guests entity with an instanced mesh and instanced skinning (walk,
  idle, talk, dance); each guest and player an entity with an agent or a controller and an
  InstanceMember, its behaviour setting the clip from its replicated `activity`. Tests per layer;
  the profiler measures 150 guests.
- **P3 The ball, solo**: the guests (Blender), the Palace first (the generator), guests
  wandering, gathering and dancing to cues, one player and bots, tagging and scoring; then the
  Canal and the Gardens, and choosing a map.
  So far: six costumes (three gowns, three coats, masked) on one rig with Idle, Walk, Talk and
  Dance; the Palace (ballroom, colonnade, courtyard, fountain) from its kit; 150 guests drawn as
  instances of their costume's mesh, one Instanced Skinning driving all six; guests stroll between
  some 40 places, gather at the nearest when the orchestra calls, and dance where they stand; the
  player steers a navigation agent, so the crowd makes way for it as for a guest. A multi-part
  guest needed a materials list on the instanced mesh (an engine change, as a mesh's).
  Then the round (Ball.as): four hunters (the player and three bots, each wearing one of six
  looks) in a ring of targets; Tag takes the nearest in reach and in front; the target scores and
  respawns elsewhere in a new costume (the engine's new `NavAgentComponent.warp`), anyone else
  costs points and flares the tagger's mask; bots grow suspicious of a target that stands out
  (sprinting, flaring, off the cue) and walk up to it, or guess; the game script gives a title,
  the HUD, pause and the results. Then the Canal (a campo on a canal with quays, two walkable
  bridges and gondolas on a loop under them, from its own Blender kit) and choosing a map on the
  title, a best round kept for each. Then the Gardens (an orangery, a terrace, a hedge maze of
  two rings round the fountain), the third map. P3 is built.
- **P4 The ball, over the network**: host and join menus, players as guests, tagging and the
  round by RPC, scores replicated; playtested with two Game tabs, then two machines and a
  browser. Built and played in two tabs (joining, moving, tagging, notes, the clock, the round's
  end and the next round).
  Design (2026-10-14, from a survey of the engine):
  - **What replicates**: every guest, the bots, the host's player, the Ball and the orchestra
    carry `net.Network` and `net.Transform`; both peers load the same scene, so authored ids match
    by the entities' guids. Guests' and players' `activity` and the cue already replicate (P1).
    Gondolas follow their path on every peer, unreplicated.
  - **Seats**: the Ball replicates each seat's owner (a player id, or a bot), costume and target,
    and the scores, time and end. An entity's active flag does not replicate, so every peer's Ball
    dresses the hunters (shows each one's look) from the replicated costumes.
  - **Hosting and joining**: the title gets Host and Join. Host picks a map, starts the server and
    loads it. Join takes an address and a map, loads the map, and connects once it is loaded, so the
    client's authored entities are registered before the host's first snapshot. The host tells a
    joiner its map; a client on another map is told so and leaves.
  - **A joining player takes a bot's seat**: the host spawns a hunter prefab for them
    (`Net.spawnFor`) where the bot stands, in its costume; the bot wanders on as a plain guest. On
    leaving, the seat goes back to the bot and the hunter is destroyed.
  - **Input to the host**: a client's own hunter sends its stick, sprint and dance (an `input` RPC,
    15 times a second and on a change) and its tags (`tag`); the host's Ball applies them to that
    player's hunter. The host's own player reads its input directly.
  - **The camera** on a client follows the player's own hunter, which tells it so when it appears.
  - **The HUD** on every peer reads the Ball's replicated state for the local seat; notes go to one
    player by `rpcTo`.
  - **Menus in a networked round** do not stop the scene (the host runs the world for everyone, and
    a joiner's scene must keep hearing it): the pause menu and the results cover a round that goes
    on, the player standing still under them. Only the host starts another round, and a joiner's
    results give way to it when the Ball's replicated end clears.
- **P5 The look and sound**: custom shaders for the masks (a sheen that shifts with the view),
  the silk, and the reveal; lantern light, the orchestra and its cues, the UI.
- **P6 Ship**: web, Steam Deck, README pictures, the website's card, a docs page.
- **P7 `wss://`** (gap 8): the client's choice of `wss://`, and a docs page on putting a TLS
  reverse proxy (Caddy, nginx) in front of a dedicated host, so the site's demo can join a hosted
  game.

## Open questions

- The round rules above (targets, sprint, reveal) are a starting point to playtest.
