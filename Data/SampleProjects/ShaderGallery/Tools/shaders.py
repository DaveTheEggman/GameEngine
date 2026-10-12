#!/usr/bin/env python3
"""shaders.py [n ...]: each lesson's shader and material, through the editor's MCP (lessons.py's
table). With lesson numbers, only those.

For each lesson: the shader file (shaders/NN_Name.hlsl) checked with shader_validate, imported into
the Shaders group (a re-run re-imports over the same asset), the generated textures imported into
Textures, a cook, then the lesson's material (Materials/<Name>, made once) put on its shader with
material_set_shader, its parameters and textures set by property name in its envelope with its
blend and cull modes, and a cook.
textures.py runs first (it writes generated/).
"""
import os, struct, sys
import xml.etree.ElementTree as ET
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from scenegen import mcp
from lessons import LESSONS, shader_file


def assets_by(type_name):
    return {a["name"]: a["guid"] for a in mcp("asset_list", {})["assets"] if a["type"] == type_name}


def import_file(path, group):
    return mcp("asset_import", {"source": path, "group": group})["guid"]


def floats(value):
    """A parameter's value as the floats its property holds (a colour or a vector, or one)."""
    return list(value) if isinstance(value, (tuple, list)) else [value]


def set_values(material_guid, params, textures, texture_ids, blend, cull):
    """The lesson's values, textures and modes into the material's envelope, by property name."""
    root = ET.fromstring(mcp("asset_data_read", {"guid": material_guid})["xml"])
    payload = root.find("object[@name='payload']")
    names = [e.text for e in payload.find("array[@name='propertyNames']")]
    offsets = [int(e.text) for e in payload.find("array[@name='propertyOffsets']")]
    defaults = payload.find("array[@name='uniformDefaults']")
    data = bytearray(int(e.text) for e in defaults)
    for name, value in params.items():
        if name not in names:
            raise SystemExit("the material has no property %s (has %s)" % (name, names))
        at = offsets[names.index(name)]
        packed = struct.pack("<%df" % len(floats(value)), *floats(value))
        data[at:at + len(packed)] = packed
    for e, b in zip(defaults, data):
        e.text = str(b)
    slots = payload.find("array[@name='textureSlots']")
    ids = payload.find("array[@name='textureIds']")
    for array in (slots, ids):
        for child in list(array):
            array.remove(child)
    for slot, texture in textures.items():
        ET.SubElement(slots, "string").text = slot
        ET.SubElement(ids, "string").text = texture_ids[texture]
    slots.set("count", str(len(textures)))
    ids.set("count", str(len(textures)))
    payload.find("u8[@name='blendMode']").text = str(blend)
    payload.find("u8[@name='cullMode']").text = str(cull)
    mcp("asset_data_write", {"guid": material_guid, "xml": ET.tostring(root, encoding="unicode")})


def main():
    only = {int(a) for a in sys.argv[1:]}
    lessons = [l for l in LESSONS if not only or l["n"] in only]

    for l in lessons:
        path = os.path.join(HERE, "shaders", shader_file(l))
        check = mcp("shader_validate", {"source": open(path).read(), "name": shader_file(l)})
        if not check["valid"]:
            raise SystemExit("%s: %s" % (shader_file(l), check["errors"]))
        import_file(path, "Shaders")
        print("shader", shader_file(l), check["kind"])
    wanted = {t for l in lessons for t in l.get("textures", {}).values()}
    for texture in sorted(wanted):
        path = os.path.join(HERE, "generated", texture)
        import_file(path + (".jpg" if os.path.exists(path + ".jpg") else ".png"), "Textures")
    mcp("asset_cook", {})

    shaders = assets_by("ShaderAsset")
    materials = assets_by("MaterialAsset")
    textures = assets_by("TextureAsset")
    for l in lessons:
        shader = shaders[os.path.splitext(shader_file(l))[0]]
        guid = materials.get(l["name"])
        if guid is None:
            guid = mcp("asset_create", {"creator": "PBR Material", "name": l["name"], "group": "Materials"})["guid"]
        mcp("material_set_shader", {"material": guid, "shader": shader})
        set_values(guid, l.get("params", {}), l.get("textures", {}), textures, l.get("blend", 0),
                   l.get("cull", 1))
        print("material", l["name"])
    mcp("asset_cook", {})


main()
