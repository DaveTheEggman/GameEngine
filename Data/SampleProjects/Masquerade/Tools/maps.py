#!/usr/bin/env python3
"""maps.py [Map ...]: writes a map's scene through the editor's MCP (the Palace for now).

A map is a ball: the ground, the walls and what stands on it from the palace kit
(blender/palace.py), each with its own unscaled collider placed from the same sizes; the lights;
the places guests wander to; the orchestra; some 150 guests; the player and the
camera; the navigation zone over it all, baked after writing.

The guests are drawn by costume: a Guests entity carries the instanced skinning (the guests' rig
and their clips: Idle, Walk, Talk, Dance, in that order, which Guest.as and Player.as count on)
and a child per costume carries that costume's instanced mesh, with no instances of its own. Each
guest is an entity of its own, a navigation agent with an Instance Member of its costume's mesh,
so every guest in a costume is one draw wherever the crowd takes them.

Metres throughout; the map's origin is its north-west corner, north being -Z. Cells (2 m: cell
(i, j) spans x 2i..2i+2, z 2j..2j+2) for the ground.

Run with the editor open on the project and its MCP server on, after the models are imported and
importscripts.py has run:
    python3 maps.py Palace
"""
import math, os, random, sys
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scenegen import Doc, mcp, yaw, pitch
from look import look

GRID = 2.0
COSTUMES = ("GownRose", "GownTeal", "GownGold", "CoatBlack", "CoatPlum", "CoatIvory")
CLIPS = ("Idle", "Walk", "Talk", "Dance")  # the clip list's order: the scripts' activity numbers
POSES_PER_CLIP = 12  # phases of each clip in the pose pool: guests on a clip spread over them

ASSETS = mcp("asset_list", {})["assets"]


def asset(asset_type, name, group=None):
    found = [a["guid"] for a in ASSETS
             if a["type"] == asset_type and a["name"] == name and (group is None or a.get("group") == group)]
    if not found:
        raise SystemExit("no %s %s (run importmodels.py / importscripts.py first)" % (asset_type, name))
    return found[0]


def model_prefab(group):
    found = [a["guid"] for a in ASSETS if a["type"] == "PrefabDocument" and a.get("group") == group]
    if not found:
        raise SystemExit("no model in %s (run the Blender script and importmodels.py)" % group)
    return found[0]


_models = {}


def model(group):
    """An imported model's parts, read from its prefab: its mesh and material slots, and (rigged)
    its skeleton."""
    if group not in _models:
        root = ET.fromstring(mcp("prefab_read", {"guid": model_prefab(group)})["xml"])
        parts = {}
        for comp in root.find("array[@name='components']"):
            kind = comp.find("string[@name='type']").text
            data = comp.find("object[@name='data']")
            if kind == "mesh":
                parts["mesh"] = data.find("string[@name='mesh']").text
                parts["materials"] = [e.text for e in data.find("array[@name='materials']")]
            elif kind == "skeletal_animation":
                parts["skeleton"] = data.find("string[@name='skeleton']").text
        _models[group] = parts
    return _models[group]


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def strings(guids):
    return ["<string>%s</string>" % g for g in guids]


class Builder:
    """One map's scene being written."""

    def __init__(self, name):
        self.name = name
        self.d = Doc(name)
        self.rng = random.Random(name)  # the same map writes the same scene
        look(self.d, "Night")

    def place(self, name, kind, pos, rot=(0, 0, 0, 1), parent=None):
        """A palace kit piece, its materials as imported."""
        parts = model("Models/Palace/" + kind)
        e = self.d.stable_id("entity")
        self.d.entity(name, pos, rot, parent=parent, eid=e)
        self.d.add(e, "mesh", mesh=parts["mesh"], materials=strings(parts["materials"]))
        return e

    def solid(self, name, pos, size, rot=(0, 0, 0, 1)):
        """A static box collider of its own, unscaled, so the shape is the size it says."""
        e = self.d.stable_id("entity")
        self.d.entity(name + "Collider", pos, rot, eid=e)
        self.d.add(e, "physics.RigidBody", motion=0, layer=0, shape=0,
                   halfExtents={"x": size[0] / 2, "y": size[1] / 2, "z": size[2] / 2})
        return e

    def ground(self, kind, cells):
        """Tiles of `kind` over cells (i0, j0, i1, j1), a slab collider under them all."""
        i0, j0, i1, j1 = cells
        for i in range(i0, i1):
            for j in range(j0, j1):
                self.place("%s%d_%d" % (kind, i, j), kind, (GRID * (i + 0.5), 0.0, GRID * (j + 0.5)))
        w, h = GRID * (i1 - i0), GRID * (j1 - j0)
        self.solid(kind, (GRID * i0 + w / 2, -0.25, GRID * j0 + h / 2), (w, 0.5, h))

    def run(self, kind, a, b, height, thick, skip=(), every=None, alt=None):
        """A run of 2 m edges from a to b (metres, along x or z): `kind` on each, `alt` on every
        `every`th; segments whose middle is in `skip` left open. One collider per stretch."""
        (x0, z0), (x1, z1) = a, b
        along_x = z0 == z1
        n = int(round(abs((x1 - x0) if along_x else (z1 - z0)) / GRID))
        sign = 1 if (x1 > x0 if along_x else z1 > z0) else -1
        stretch = []

        def close():
            if stretch:
                lo, hi = stretch[0] - GRID / 2, stretch[-1] + GRID / 2
                mid, length = (lo + hi) / 2, hi - lo
                pos = (mid, height / 2, z0) if along_x else (x0, height / 2, mid)
                size = (length, height, thick) if along_x else (thick, height, length)
                self.solid("%s%d_%d" % (kind, int(mid), int(z0 if along_x else x0)), pos, size)
                stretch.clear()

        for s in range(n):
            c = (x0 if along_x else z0) + sign * GRID * (s + 0.5)
            at = (c, 0.0, z0) if along_x else (x0, 0.0, c)
            if (at[0], at[2]) in skip:
                close()
                continue
            piece = alt if (every and alt and s % every == every // 2) else kind
            self.place("%s%d_%d" % (piece, int(at[0]), int(at[2])), piece, at,
                       (0, 0, 0, 1) if along_x else yaw(90.0))
            stretch.append(c)
        close()

    def prop(self, kind, pos, size, rot=(0, 0, 0, 1), name=None):
        """A kit piece standing on the ground with a box collider of `size` (x, height, z)."""
        e = self.place(name or "%s%d_%d" % (kind, int(pos[0]), int(pos[2])), kind, pos, rot)
        self.solid(kind + "%d_%d" % (int(pos[0]), int(pos[2])), (pos[0], size[1] / 2, pos[2]),
                   size, rot)
        return e

    def light(self, name, pos, color, intensity, rng, shadows=False, parent=None):
        e = self.d.stable_id("entity")
        self.d.entity(name, pos, parent=parent, eid=e)
        self.d.add(e, "light", type=1, color=color, intensity=intensity, range=rng, castsShadows=shadows)
        return e

    def lantern(self, x, z):
        post = self.prop("Lantern", (x, 0.0, z), (0.4, 2.6, 0.4))
        self.light("LanternLight%d_%d" % (int(x), int(z)), (0, 2.62, 0),
                   {"r": 1.0, "g": 0.74, "b": 0.45, "a": 1.0}, 5.0, 8.0, parent=post)

    def chandelier(self, x, z, height):
        e = self.place("Chandelier%d" % int(x), "Chandelier", (x, height, z))
        self.light("ChandelierLight%d" % int(x), (0, -0.9, 0), {"r": 1.0, "g": 0.80, "b": 0.55, "a": 1.0},
                   9.0, 13.0, parent=e)

    def points(self, name, spots):
        """A parent entity whose children are places (the guests' `spots`)."""
        parent = self.d.stable_id("entity")
        self.d.entity(name, eid=parent)
        for k, (x, z) in enumerate(spots):
            self.d.entity("%s%d" % (name[:-1] if name.endswith("s") else name, k), (x, 0.0, z), parent=parent)
        return parent

    # ---- the guests ----
    def crowd(self, count, spots, player_at):
        """The Guests entity (instanced skinning over a costume mesh each), the guests, the player
        and the camera."""
        d = self.d
        first = model("Models/Guests/Guest" + COSTUMES[0])
        clips = [asset("AnimationClipAsset", c, "Models/Guests/Guest" + COSTUMES[0]) for c in CLIPS]
        guests = d.stable_id("entity")
        d.entity("Guests", eid=guests)
        meshes = {}
        for c in COSTUMES:
            parts = model("Models/Guests/Guest" + c)
            e = d.stable_id("entity")
            d.entity("Costume" + c, parent=guests, eid=e)
            d.add(e, "instanced_mesh", mesh=parts["mesh"], materials=strings(parts["materials"]), instances=[],
                  poseAssignment=0)
            meshes[c] = e
        d.add(guests, "instanced_skinning", skeleton=first["skeleton"], clips=strings(clips),
              poseCount=POSES_PER_CLIP, targets=strings(meshes[c] for c in COSTUMES))

        guest_script = asset("ScriptClassAsset", "Guest")
        spot_list = [p for p in spots]
        for n in range(count):
            x, z = self.rng.choice(spot_list)
            a, r = self.rng.uniform(-math.pi, math.pi), 1.6 * math.sqrt(self.rng.uniform(0.05, 1.0))
            e = d.stable_id("entity")
            d.entity("Guest%d" % (n + 1), (x + math.sin(a) * r, 0.0, z + math.cos(a) * r),
                     yaw(self.rng.uniform(-180, 180)), eid=e)
            self.person(e, meshes[self.rng.choice(COSTUMES)])
            d.script(e, (guest_script, {"spots": ("entity", self.spots)}))

        x, z = player_at
        player = d.stable_id("entity")
        d.entity("Player", (x, 0.0, z), eid=player)
        self.person(player, meshes[self.rng.choice(COSTUMES)])
        cam = d.stable_id("entity")
        d.entity("Camera", (x, 9.0, z + 7.0), pitch(-48.0), eid=cam)
        d.add(cam, "camera", fovYRadians=0.9)
        d.script(player, (asset("ScriptClassAsset", "Player"), {"camera": ("entity", cam)}))
        d.script(cam, (asset("ScriptClassAsset", "CameraRig"), {"target": ("entity", player), "distance": 7.0,
                                                                 "height": 9.0}))

    def person(self, e, costume_mesh):
        """A guest's body: an agent the crowd steers, drawn as one of its costume's instances."""
        self.d.add(e, "navigation.Agent", radius=0.3, height=1.7, maxSpeed=3.2, maxAcceleration=6.0)
        self.d.add(e, "instance_member", instancedMesh=costume_mesh, clip=0)

    def navigation(self, bounds):
        x0, z0, x1, z1 = bounds
        e = self.d.stable_id("entity")
        self.d.entity("NavZone", ((x0 + x1) / 2, 1.5, (z0 + z1) / 2), eid=e)
        found = [a["guid"] for a in ASSETS if a["type"] == "NavigationZoneAsset" and a["name"] == self.name + "Nav"]
        zone = found[0] if found else mcp("asset_create", {"creator": "Navigation Zone", "name": self.name + "Nav",
                                                           "group": "Navigation"})["guid"]
        # Fine enough that the gaps between the colonnade's columns stay open for a guest's radius,
        # and flat enough (the cell height) that guests stand on the floor, not above it. A guest
        # steps up nothing taller than a kerb: the fountain's basin, the benches and the dais are
        # in the way, not walked over.
        self.d.add(e, "navigation.Zone", extents={"x": (x1 - x0) / 2, "y": 3.0, "z": (z1 - z0) / 2}, zone=zone,
                   agentRadius=0.3, cellSize=0.2, cellHeight=0.05, agentMaxClimb=0.25, maxAgents=256)

    def write(self):
        scenes = [a for a in ASSETS if a["type"] == "SceneDocument" and a["name"] == self.name]
        return self.d.write(scenes[0]["guid"] if scenes else None, group="Scenes")


def palace():
    """The Palace: a ballroom on the north side (x 8..36, z 2..16, marble, windows down its walls,
    chandeliers, the orchestra's dais), open to the courtyard south of it through a colonnade; the
    courtyard (x 4..40, z 16..44, flagstones) round a fountain, lanterns, orange trees in pots and
    benches, inside a balustrade with a gate in its south side."""
    b = Builder("Palace")
    d = b.d
    moon = d.stable_id("entity")
    d.entity("Moon", rot=quat_mul(yaw(-60.0), pitch(-50.0)), eid=moon)
    d.add(moon, "light", type=0, color={"r": 0.55, "g": 0.65, "b": 1.0, "a": 1.0}, intensity=0.35,
          castsShadows=True)

    b.ground("Marble", (4, 1, 18, 8))
    b.ground("Flags", (2, 8, 20, 22))
    # The ballroom's walls, a window every other edge, piers at the corners.
    b.run("Wall", (8, 2), (36, 2), 5.0, 0.4, every=2, alt="Window")
    b.run("Wall", (8, 2), (8, 16), 5.0, 0.4, every=2, alt="Window")
    b.run("Wall", (36, 2), (36, 16), 5.0, 0.4, every=2, alt="Window")
    for x, z in ((8, 2), (36, 2), (8, 16), (36, 16)):
        b.prop("Pier", (x, 0.0, z), (0.6, 5.2, 0.6))
    for k in range(7):  # the colonnade, open to the courtyard
        b.prop("Column", (10.0 + 4.0 * k, 0.0, 16.0), (0.6, 5.0, 0.6))
    # The courtyard's balustrade: west, east, south (the gate's two edges left open), and the short
    # runs either side of the ballroom.
    b.run("Balustrade", (4, 16), (4, 44), 1.0, 0.3)
    b.run("Balustrade", (40, 16), (40, 44), 1.0, 0.3)
    b.run("Balustrade", (4, 44), (40, 44), 1.0, 0.3, skip={(21.0, 44.0), (23.0, 44.0)})
    b.run("Balustrade", (4, 16), (8, 16), 1.0, 0.3)
    b.run("Balustrade", (36, 16), (40, 16), 1.0, 0.3)

    # The fountain: an octagon of two boxes for its basin.
    b.place("Fountain", "Fountain", (22.0, 0.0, 30.0))
    b.solid("Fountain", (22.0, 0.35, 30.0), (5.6, 0.7, 5.6))
    b.solid("FountainTurned", (22.0, 0.35, 30.0), (5.6, 0.7, 5.6), yaw(45.0))
    for k in range(6):
        t = 2 * math.pi * (k + 0.5) / 6
        b.lantern(22.0 + 6.5 * math.cos(t), 30.0 + 6.5 * math.sin(t))
    for x, z in ((6.5, 18.5), (37.5, 18.5), (6.5, 41.5), (37.5, 41.5)):
        b.lantern(x, z)
    for z in (21.0, 27.0, 33.0, 39.0):
        b.prop("Planter", (5.4, 0.0, z), (0.8, 2.2, 0.8))
        b.prop("Planter", (38.6, 0.0, z), (0.8, 2.2, 0.8))
    for x, z, turn in ((14.0, 24.0, 135.0), (30.0, 24.0, -135.0), (14.0, 36.0, 45.0), (30.0, 36.0, -45.0)):
        b.prop("Bench", (x, 0.0, z), (1.6, 0.45, 0.5), yaw(turn))

    for x in (15.0, 22.0, 29.0):
        b.chandelier(x, 9.5, 5.2)
    dais = b.prop("Dais", (22.0, 0.0, 3.8), (6.0, 0.4, 3.0), name="Dais")
    d.script(dais, (asset("ScriptClassAsset", "Orchestra"), {}))

    # The places guests go: groups across the courtyard, along the ballroom's walls and under the
    # colonnade, some 40 for 150 guests (a few to a group).
    spots = [(10, 20), (16, 20), (28, 20), (34, 20), (8, 28), (36, 28), (10, 38), (16, 40), (28, 40), (34, 38),
             (22, 39), (22, 21), (11, 7), (11, 13), (33, 7), (33, 13), (17, 13), (27, 13),
             (8, 23), (36, 23), (8, 34), (36, 34), (13, 30), (31, 30), (16, 26), (28, 26), (16, 34), (28, 34),
             (12, 18), (32, 18), (22, 18), (8, 42), (36, 42), (16, 41.5), (28, 41.5), (14, 5), (30, 5)]
    # Round the fountain, far enough out that a group spread round a place stays off the basin.
    spots += [(22 + 5.8 * math.cos(2 * math.pi * k / 8), 30 + 5.8 * math.sin(2 * math.pi * k / 8)) for k in range(8)]
    b.spots = b.points("Spots", spots)
    b.crowd(150, spots, (22.0, 42.0))
    b.navigation((3.0, 1.0, 41.0, 45.0))
    return b.write()


MAPS = {"Palace": palace}


def write(name):
    guid = MAPS[name]()
    # The navigation for the crowd: baked from the scene's static colliders into <Map>Nav.
    mcp("page_open", {"guid": guid})
    print(name, guid, mcp("navigation_bake", {"page": guid, "entity": "NavZone"}).get("triangles"), "nav triangles")
    return guid


if __name__ == "__main__":
    names = sys.argv[1:] or list(MAPS)
    first = None
    for n in names:
        g = write(n)
        first = first or g
    if names and names[0] == "Palace":
        mcp("project_settings_set", {"defaultSceneId": first})  # the game boots into the first map for now
    mcp("asset_cook", {})
