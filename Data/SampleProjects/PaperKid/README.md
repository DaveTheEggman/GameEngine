# PaperKid

A small arcade game built as the engine's tracked **editor sample project** - a vertical slice that
exercises the game-ready runtime end to end (scene + navigation + property animation + a scripted
run/Game tier + game UI), authored in-editor so it dogfoods the whole authoring stack.

## The game

Ride a bike around a town block delivering papers against the clock. Each level hands you a stack of
papers and a countdown: find the marked subscriber houses in free roam, throw papers into their
delivery zones to hit the level's quota, and avoid traffic, pedestrians, and street junk (a crash
costs speed and time - arcade-recoverable, not instant death). Clear the quota before the timer runs
out to advance; blocks get bigger, busier, and tighter on time as you go. Third-person follow camera,
primitive-blockout art, a score-chasing loop - not a sim.

## Status

PLAN / scaffolding. The gameplay is specced in `Documentation/Specs/paperkid.md`; its scripted run
tier depends on the `game-ready-scripting2` track landing first. Scripts are **AngelScript** (Luau is
the other supported backend); obstacles use the navigation system; tells / markers / camera use
property-animation clips.

## Project layout

- `Project.xml` - the project manifest.
- `Sources/` - raw imported source assets (fonts, textures, ...).
- `Content/` - the asset envelopes (`*.xasset`) that define the project's assets.
- `Cooked/`, `.cache/`, `Editor/` - generated output + per-user editor state; **gitignored** (the
  tools regenerate them from the source above). See `.gitignore`.

Open it from the engine's project manager / editor.
