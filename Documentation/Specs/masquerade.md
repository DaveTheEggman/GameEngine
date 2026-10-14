# Masquerade - the fifth sample, social stealth in a crowd over the network

> STATUS: APPROVED 2026-10-14. P0 (network commands for scripts) and P1 (replicated state) BUILT
> 2026-10-14; P2 next. Agreed (user 2026-10-14): the masked
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
- **P2 Crowds at scale** (gaps 6 and 7): the agent limit a zone setting; a crowd that moves
  instanced skinned guests with navigation agents, each guest choosing its clip (walk, idle, talk,
  dance) from per-clip pose pools; a script facade. Tests: agents drive their instances, a clip
  change moves an instance to that clip's pool; the profiler measures 150 guests.
- **P3 The ball, solo**: the guests (Blender), the Palace first (the generator), guests
  wandering, gathering and dancing to cues, one player and bots, tagging and scoring; then the
  Canal and the Gardens, and choosing a map.
- **P4 The ball, over the network**: host and join menus, players as guests, tagging and the
  round by RPC, scores replicated; playtested with two Game tabs, then two machines and a
  browser.
- **P5 The look and sound**: custom shaders for the masks (a sheen that shifts with the view),
  the silk, and the reveal; lantern light, the orchestra and its cues, the UI.
- **P6 Ship**: web, Steam Deck, README pictures, the website's card, a docs page.
- **P7 `wss://`** (gap 8): the client's choice of `wss://`, and a docs page on putting a TLS
  reverse proxy (Caddy, nginx) in front of a dedicated host, so the site's demo can join a hosted
  game.

## Open questions

- The round rules above (targets, sprint, reveal) are a starting point to playtest.
