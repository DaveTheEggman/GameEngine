# 0.2 backlog - open items for after the 0.1 release

> Work decided for 0.2, not to be looked at for 0.1. Started 2026-10-10 with items moved from
> `weekly_backlog.md`; `backlog-0.1.md` holds the release's work. Pull an item out of here into
> the current weekly when it is started; delete it here when done.

## A material / shader node graph (user 2026-10-12; moved from 0.1)

The tier-2 half of the 0.1 custom-shader item: visual nodes (texture sample, math, blend, ... to a
surface output) that GENERATE a surface shader, fed through the same cook (variants, entry
points, bytecode packs), on the NodeGraph UI and the AnimationGraphPage pattern. It builds on
0.1's surface shaders (`Specs/custom-shaders.md`): a graph is a surface function written for you.

## Android: a native player and its export (user 2026-10-11)

Games run on phones today through the web export (touch drives the game UI, touch controls with
their guide, 0.1). A native Android player is the 0.2 step: a game shipped as an app, on Vulkan,
without a browser's limits. Moved here from `weekly_backlog.md` ("Android: shell backend +
touch/input + build"). The pieces:

- **Shell.** The desktop shell is SDL3, which runs on Android: its window, lifecycle (pause,
  resume, a lost surface on going to the background), touch (already normalized, with `pointerX/Y`,
  the contract the web and desktop shells keep) and the back button.
- **Rendering.** Vulkan (the RHI's backend); the surface's `currentTransform` drives the present
  transform (`Specs/present-transform.md` section "Native, later": `preTransform = currentTransform`,
  the extent swapped, the content turned), so the compositor does no rotation work.
- **Build and export.** An Android export template (NDK, Gradle, an APK or AAB), the player and its
  sidecars as the desktop and web templates are; an export preset (Android) with its icon, its
  orientation (the spec's `ScreenOrientation`) and signing; content in the APK's assets read through
  the VFS.
- **Saves and files.** The user data directory on Android's app storage.
- **Not in it.** The editor stays desktop; on-device MCP and play-in-editor are out.

Check against a real device (the user's phone), as the web demos were: touch, rotation, pause and
resume, the back button.

## Seeded: mountable content directories (user 2026-08-29)

Origin: reviewing Sedulous's VFS while building the editor's `data://` discovery+mount. Sedulous lets
a project MOUNT MULTIPLE content directories. The ask: allow an editor project to mount additional
content dirs beyond its own Sources/ - so authored content can be packaged (zip a data dir), shipped
to someone else, and they DOWNLOAD + MOUNT it into their editor project (asset packs, shared prefabs,
sample content). Builds on foundation.vfs (schemes/mounts) + the content DB.

Sketch: a project references N mounted content roots (its own + external packs); the content DB and
asset browser union them; GUID stability across packs (a pack's assets keep their guids so references
resolve after mounting). Decide: mount persistence (in project settings), conflict handling (same
guid in two packs), whether external mounts are read-only, and product/cook handling for mounted
sources. Relates to the `data://` discovery work + [[vfs-and-resource-stack]].
