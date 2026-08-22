Organize new menu

Drag instance into group

seed default assets + set them up in project settings automatically for new projects.

do we still use the loading screen project setting now that we have a new ui setup

Should we store sml and sss payload in sources?
Everything is imported in the editor asset and by hand edits are ugly.
perhaps we need to make them like scripts where the actual source lives in the external file in Sources?  -- resolved, only follow up is asset page for sss files.

Seems like I can't shift+down/up select more than 2 items.

We need string Guid for editor text document usage.

Need right click copy guid/path for instance.

Better default input map - movement wasd/left down right up.

PIE should wait for cook to finish before starting.

button heights are squished on high dpi monitor, not all buttons, affects toolbar buttons in editor like the scene toolbar and anim panel in the bottom.

debug panel in game page flows off screen in high dpi monitor.

slider numeric fields are squished in the width on high dpi monitor.

All these high dpi tests are at 1.75 scaling.
I guess just scaling up to 1.75 shows the problem on non-highdpi. the scene page toolbar, the buttons shrink, but icons keep size/scale up.

Adding collision group does not refresh the property so it isn't immediately visible.
Needs a better UI anyway.

Start dragging detached window, it doesn't stop dragging. - Not in all tests.

Detached window doesn't render viewport unless main window is visible.

Open export settings, press tab in a field and the PIE game is handling it. I guess this is different from surface gating, because the game may be handling an input binding? Is that supposed to be gated too? or is it just device input that's gated?

exported player cannot find run::loadScene and run::requestExit
