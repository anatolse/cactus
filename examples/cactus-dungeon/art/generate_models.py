"""Generate the heart and minimap marker models as self-contained GLB files."""

import json
from pathlib import Path
import struct


HERE = Path(__file__).resolve().parent
FACES = [
    ((1, 0, 0), [(1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1)]),
    ((-1, 0, 0), [(-1, -1, 1), (-1, 1, 1), (-1, 1, -1), (-1, -1, -1)]),
    ((0, 1, 0), [(-1, 1, -1), (-1, 1, 1), (1, 1, 1), (1, 1, -1)]),
    ((0, -1, 0), [(-1, -1, 1), (-1, -1, -1), (1, -1, -1), (1, -1, 1)]),
    ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),
    ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),
]
COLORS = {
    "steel": [0.64, 0.7, 0.76, 1.0],
    "dark": [0.08, 0.11, 0.16, 1.0],
    "light": [0.92, 0.96, 1.0, 1.0],
    "gold": [1.0, 0.72, 0.15, 1.0],
    "white": [1.0, 1.0, 1.0, 1.0],
}


def model(parts):
    groups = {}
    for color, center, size in parts:
        positions, normals, indices = groups.setdefault(color, ([], [], []))
        for normal, corners in FACES:
            start = len(positions) // 3
            for corner in corners:
                positions.extend(center[i] + corner[i] * size[i] * 0.5 for i in range(3))
                normals.extend(normal)
            indices.extend(start + i for i in [0, 1, 2, 0, 2, 3])
    return encode_groups(groups)


def encode_groups(groups):
    binary = bytearray()
    views = []
    accessors = []
    primitives = []
    materials = []
    for color, (positions, normals, indices) in groups.items():
        attributes = {}
        for name, values, code, component, width in [
            ("POSITION", positions, "f", 5126, 3),
            ("NORMAL", normals, "f", 5126, 3),
            ("indices", indices, "H", 5123, 1),
        ]:
            while len(binary) % 4:
                binary.append(0)
            encoded = struct.pack("<" + code * len(values), *values)
            views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(encoded),
                          "target": 34963 if name == "indices" else 34962})
            accessor = {"bufferView": len(views) - 1, "componentType": component,
                        "count": len(values) // width, "type": "SCALAR" if width == 1 else "VEC3"}
            if name == "POSITION":
                accessor["min"] = [min(values[i::3]) for i in range(3)]
                accessor["max"] = [max(values[i::3]) for i in range(3)]
            accessors.append(accessor)
            attributes[name] = len(accessors) - 1
            binary.extend(encoded)
        primitives.append({"attributes": {k: v for k, v in attributes.items() if k != "indices"},
                           "indices": attributes["indices"], "material": len(materials)})
        materials.append({"pbrMetallicRoughness": {"baseColorFactor": COLORS[color],
                                                   "metallicFactor": 0.0, "roughnessFactor": 0.8}})
    document = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
                "nodes": [{"mesh": 0}], "meshes": [{"primitives": primitives}], "materials": materials,
                "buffers": [{"byteLength": len(binary)}], "bufferViews": views, "accessors": accessors}
    text = json.dumps(document, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    chunks = struct.pack("<II", len(text), 0x4E4F534A) + text
    chunks += struct.pack("<II", len(binary), 0x004E4942) + binary
    return struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks


HEART = [("light", ((x - 3) * 0.22, (4 - y) * 0.22, 0), (0.22, 0.22, 0.35))
         for y, row in enumerate(["0110110", "1111111", "0111110", "0011100", "0001000"])
         for x, cell in enumerate(row) if cell == "1"]


if __name__ == "__main__":
    for name, parts in [("heart", HEART)]:
        (HERE / f"{name}.glb").write_bytes(model(parts))
    corners = FACES[4][1]
    positions = [corner[i] * 0.5 if i < 2 else 0.0 for corner in corners for i in range(3)]
    normals = [0.0, 0.0, 1.0] * 4
    (HERE / "quad.glb").write_bytes(encode_groups({"white": (positions, normals, [0, 1, 2, 0, 2, 3])}))
