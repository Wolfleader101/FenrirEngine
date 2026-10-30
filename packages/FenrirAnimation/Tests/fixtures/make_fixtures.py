"""Writes FenrirAnimation's glTF fixtures beside this script. Run by hand (`python make_fixtures.py`) from any directory;
the files are checked in and the build never runs it. Everything here is written from scratch, so the fixtures carry no
third-party licence.

rig.gltf, the headless bone check's rig (values the test compares against are worked out in its comments):
  Rig          the skeleton's owner, at the origin
    Shoulder   a bone at T(0, 1, 0)
      Elbow    a bone at T(0, 1, 0)
    Body       a triangle skinned to Shoulder and Elbow
  "Swing": over 0 to 1 s, LINEAR, Shoulder's rotation from identity to 90 degrees about +Z, and Elbow's translation
  from (0, 1, 0) to (0, 2, 0).
  "Turn": over 0 to 2 s, CUBICSPLINE, Shoulder's rotation with keys at 0, 1 and 2 s, key 1 written in the far
  hemisphere from key 0 with its tangents (the importer flips them together; FenrirAssets's cubic-clip.gltf has the
  same keys).

twins.gltf, for binding by node identity:
  Rig > Twin > Twin   two bones with one name, each at T(0, 1, 0), and Body skinned to both
  Prop                an ordinary node under Rig
  "Lift": LINEAR over 0 to 1 s, the lower Twin's translation from (0, 1, 0) to (0, 3, 0), and Prop's translation from
  (0, 0, 0) to (1, 0, 0).
"""

import base64
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))


class Buffer:
    """One binary buffer, with an accessor added per array."""

    def __init__(self, doc, name):
        self.doc = doc
        self.name = name
        self.data = bytearray()
        doc.update({"buffers": [], "bufferViews": [], "accessors": []})

    def accessor(self, packed, count, component, kind, minmax=None):
        while len(self.data) % 4:
            self.data.append(0)
        self.doc["bufferViews"].append({"buffer": 0, "byteOffset": len(self.data), "byteLength": len(packed)})
        self.data += packed
        accessor = {"bufferView": len(self.doc["bufferViews"]) - 1, "componentType": component, "count": count,
                    "type": kind}
        if minmax:
            accessor["min"], accessor["max"] = minmax
        self.doc["accessors"].append(accessor)
        return len(self.doc["accessors"]) - 1

    def floats(self, rows, kind, bounds=True):
        flat = [float(v) for row in rows for v in row]
        minmax = None
        if bounds:
            width = len(rows[0])
            minmax = ([min(r[i] for r in rows) for i in range(width)], [max(r[i] for r in rows) for i in range(width)])
        return self.accessor(struct.pack("<%df" % len(flat), *flat), len(rows), 5126, kind, minmax)

    def joints(self, rows):
        flat = [v for row in rows for v in row]
        return self.accessor(struct.pack("<%dB" % len(flat), *flat), len(rows), 5121, "VEC4")

    def indices(self, values):
        return self.accessor(struct.pack("<%dH" % len(values), *values), len(values), 5123, "SCALAR")

    def finish(self):
        self.doc["buffers"] = [{"byteLength": len(self.data), "uri": self.name + ".bin"}]
        with open(os.path.join(HERE, self.name + ".bin"), "wb") as out:
            out.write(self.data)
        with open(os.path.join(HERE, self.name + ".gltf"), "w", newline="\n", encoding="utf-8") as out:
            json.dump(self.doc, out, indent=1)
            out.write("\n")


def quat_z(degrees):
    half = math.radians(degrees) / 2
    return (0.0, 0.0, math.sin(half), math.cos(half))


def translate(t):
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t[0], t[1], t[2], 1]


def document(name):
    doc = {"asset": {"version": "2.0", "generator": "FenrirEngine FenrirAnimation make_fixtures.py"}, "nodes": []}
    return doc, Buffer(doc, name)


def node(doc, name, parent=None, **fields):
    doc["nodes"].append(dict({"name": name}, **fields))
    index = len(doc["nodes"]) - 1
    if parent is not None:
        doc["nodes"][parent].setdefault("children", []).append(index)
    return index


def skinned_triangle(doc, buf, joints, inverse_binds):
    doc["skins"] = [{"joints": joints, "inverseBindMatrices": buf.floats(inverse_binds, "MAT4", False)}]
    positions = [(0, 1, 0), (0.5, 1.5, 0), (0, 2, 0)]
    doc["meshes"] = [{"name": "Body", "primitives": [{"attributes": {
        "POSITION": buf.floats(positions, "VEC3"),
        "JOINTS_0": buf.joints([(0, 1, 0, 0)] * 3),
        "WEIGHTS_0": buf.floats([(0.5, 0.5, 0, 0)] * 3, "VEC4")}, "indices": buf.indices([0, 1, 2])}]}]


def channel(doc, buf, animation, target, path, times, values, interpolation="LINEAR"):
    animation["samplers"].append({"input": buf.floats([(t,) for t in times], "SCALAR"),
                                  "output": buf.floats(values, "VEC4" if path == "rotation" else "VEC3", False),
                                  "interpolation": interpolation})
    animation["channels"].append({"sampler": len(animation["samplers"]) - 1, "target": {"node": target, "path": path}})


def write_rig():
    doc, buf = document("rig")
    rig = node(doc, "Rig")
    shoulder = node(doc, "Shoulder", rig, translation=[0, 1, 0])
    elbow = node(doc, "Elbow", shoulder, translation=[0, 1, 0])
    node(doc, "Body", rig, mesh=0, skin=0)
    skinned_triangle(doc, buf, [shoulder, elbow], [translate((0, -1, 0)), translate((0, -2, 0))])
    swing = {"name": "Swing", "samplers": [], "channels": []}
    channel(doc, buf, swing, shoulder, "rotation", [0, 1], [quat_z(0), quat_z(90)])
    channel(doc, buf, swing, elbow, "translation", [0, 1], [(0, 1, 0), (0, 2, 0)])
    turn = {"name": "Turn", "samplers": [], "channels": []}
    edge_in, edge_out = (0, 0, 0.05, 0), (0, 0, 0.02, 0)
    channel(doc, buf, turn, shoulder, "rotation", [0, 1, 2],
            [edge_in, (0, 0, 0, 1), edge_out,
             (0, 0, 0.1, 0.2), (0, 0, -0.3826834, -0.9238795), (0, 0, -0.1, 0.3),
             edge_in, (0, 0, 0.7071068, 0.7071068), edge_out], "CUBICSPLINE")
    doc["animations"] = [swing, turn]
    doc["scenes"] = [{"nodes": [rig]}]
    doc["scene"] = 0
    buf.finish()


def write_twins():
    doc, buf = document("twins")
    rig = node(doc, "Rig")
    upper = node(doc, "Twin", rig, translation=[0, 1, 0])
    lower = node(doc, "Twin", upper, translation=[0, 1, 0])
    prop = node(doc, "Prop", rig)
    node(doc, "Body", rig, mesh=0, skin=0)
    skinned_triangle(doc, buf, [upper, lower], [translate((0, -1, 0)), translate((0, -2, 0))])
    lift = {"name": "Lift", "samplers": [], "channels": []}
    channel(doc, buf, lift, lower, "translation", [0, 1], [(0, 1, 0), (0, 3, 0)])
    channel(doc, buf, lift, prop, "translation", [0, 1], [(0, 0, 0), (1, 0, 0)])
    doc["animations"] = [lift]
    doc["scenes"] = [{"nodes": [rig]}]
    doc["scene"] = 0
    buf.finish()


if __name__ == "__main__":
    write_rig()
    write_twins()
