#!/usr/bin/env python3
"""Writes the fixtures of testUsdGltfFileFormat (standard library only).

    python makeFixtures.py   # writes testUsdGltfFileFormat/

- box.glb:            one box mesh (glTF Y-up, metres): x in [-2, 2], y in [0, 1],
                      z in [-0.5, 0.5]; 8 points, 12 triangles.
- box.gltf + box.bin: the same box as glTF JSON with an external buffer.
- nodes.glb:          the box under a node with translation (10, 0, -5),
                      rotation 90 degrees about +Y and scale (2, 1, 1).
- wrapper.usda:       references box.glb under a prim translated by (100, 0, 0),
                      as the geospatial web app's wrapper layers do.
"""
import json
import math
import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent / "testUsdGltfFileFormat"

POINTS = [(-2, 0, -0.5), (2, 0, -0.5), (2, 0, 0.5), (-2, 0, 0.5),
          (-2, 1, -0.5), (2, 1, -0.5), (2, 1, 0.5), (-2, 1, 0.5)]
INDICES = [0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
           1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0]


def buffer():
    pbin = b"".join(struct.pack("<3f", *p) for p in POINTS)
    ibin = b"".join(struct.pack("<H", i) for i in INDICES)
    ibin += b"\0" * (-len(ibin) % 4)
    return pbin, pbin + ibin


def gltf(nodes, uri=None):
    pbin, binary = buffer()
    doc = {
        "asset": {"version": "2.0", "generator": "testUsdGltfFileFormat makeFixtures.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": nodes,
        "meshes": [{"name": "Box", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "buffers": [dict({"byteLength": len(binary)}, **({"uri": uri} if uri else {}))],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(pbin)},
                        {"buffer": 0, "byteOffset": len(pbin), "byteLength": len(INDICES) * 2}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(POINTS),
                       "type": "VEC3", "min": [-2, 0, -0.5], "max": [2, 1, 0.5]},
                      {"bufferView": 1, "componentType": 5123, "count": len(INDICES),
                       "type": "SCALAR"}],
    }
    return doc, binary


def glb(doc, binary):
    js = json.dumps(doc).encode()
    js += b" " * (-len(js) % 4)
    out = struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(js) + 8 + len(binary))
    out += struct.pack("<I4s", len(js), b"JSON") + js
    return out + struct.pack("<I4s", len(binary), b"BIN\0") + binary


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "box.glb").write_bytes(glb(*gltf([{"name": "BoxNode", "mesh": 0}])))

    doc, binary = gltf([{"name": "BoxNode", "mesh": 0}], uri="box.bin")
    (OUT / "box.gltf").write_text(json.dumps(doc, indent=1) + "\n")
    (OUT / "box.bin").write_bytes(binary)

    s = math.sin(math.radians(45))
    (OUT / "nodes.glb").write_bytes(glb(*gltf([
        {"name": "Placed", "translation": [10, 0, -5], "rotation": [0, s, 0, s],
         "scale": [2, 1, 1], "children": [1]},
        {"name": "BoxNode", "mesh": 0}])))

    (OUT / "wrapper.usda").write_text('''#usda 1.0
(
    defaultPrim = "Wrapper"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "Wrapper"
{
    def Xform "Model" (
        prepend references = @./box.glb@
    )
    {
        double3 xformOp:translate = (100, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
''')
    print("wrote {}".format(OUT))


if __name__ == "__main__":
    main()
