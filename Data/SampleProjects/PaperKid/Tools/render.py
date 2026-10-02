#!/usr/bin/env python3
"""render.py <Name>...: scripts/<Name>.as with {{Asset}} placeholders -> PaperKid's Sources/<Name>.as
with the asset ids, then script_validate over the editor's MCP."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pkgen import mcp
HERE = os.path.dirname(os.path.abspath(__file__))
SOURCES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Sources")
# By name; a prefab sharing a script's name is written {{Prefab:Name}}.
ids = {}
for a in mcp("asset_list", {})["assets"]:
    ids[("Prefab:" if a["type"] == "PrefabDocument" else "") + a["name"]] = a["guid"]
ok = True
for name in sys.argv[1:]:
    text = open(os.path.join(HERE, "scripts", name + ".as")).read()
    text = re.sub(r"\{\{([^}]+)\}\}", lambda m: ids[m.group(1)], text)
    open(os.path.join(SOURCES, name + ".as"), "w").write(text)
    v = mcp("script_validate", {"source": text, "language": "angelscript", "name": name + ".as"})
    print(name, "valid" if v["valid"] else "INVALID", v.get("properties"), v.get("handlers"))
    for e in v.get("errors", []):
        print("   ", e)
    ok = ok and v["valid"]
sys.exit(0 if ok else 1)
