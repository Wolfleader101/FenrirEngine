"""Writes AnimationDemo's procedural stand-in models (ATTRIBUTION.md): mannequin.gltf, a blocky humanoid with Idle,
Walk, Run, Wave and Flutter clips on its own rig, and critter.gltf, a small four-legged rig with a long tail and a Wag
clip. Run by hand (`python make_assets.py`) from any directory; the files are written beside this script and checked
in. The build never runs it.

Both are written from scratch by this script, so they carry no third-party licence (MIT, as the repository). They stand
in for the spec's Quaternius humanoid and Khronos Fox until those files are checked and added.

Each model: an "Armature" node (the skeleton's owner) holding the root bone and a skinned mesh node. Every bone gets a
box from its joint towards its child; a box's vertices at the child end are weighted half to the child bone, so joints
bend smoothly. Bones are at rest with no rotation, so each inverse bind is the inverse of the bone's rest translation in
model space. Clips are linear rotation keys (and a translation bob on the hips).
"""

import base64
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))


def quat(axis, degrees):
    """A unit quaternion (x, y, z, w) turning `degrees` about `axis`."""
    length = math.sqrt(sum(c * c for c in axis))
    half = math.radians(degrees) / 2.0
    s = math.sin(half) / length
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


class Buffer:
    """One glTF buffer, embedded as a data URI, with its views and accessors appended to the document."""

    def __init__(self, doc):
        self.doc = doc
        self.data = bytearray()
        doc["bufferViews"] = []
        doc["accessors"] = []

    def view(self, payload, target=None):
        while len(self.data) % 4:
            self.data.append(0)
        view = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target:
            view["target"] = target
        self.data += payload
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, rows, component_type, fmt, kind, target=None, bounds=False):
        flat = [c for row in rows for c in row]
        view = self.view(struct.pack("<%d%s" % (len(flat), fmt), *flat), target)
        accessor = {"bufferView": view, "componentType": component_type, "count": len(rows), "type": kind}
        if bounds:
            width = len(rows[0])
            accessor["min"] = [min(r[i] for r in rows) for i in range(width)]
            accessor["max"] = [max(r[i] for r in rows) for i in range(width)]
        self.doc["accessors"].append(accessor)
        return len(self.doc["accessors"]) - 1

    def floats(self, rows, kind, target=34962, bounds=False):
        return self.accessor(rows, 5126, "f", kind, target, bounds)

    def finish(self):
        uri = "data:application/octet-stream;base64," + base64.b64encode(bytes(self.data)).decode("ascii")
        self.doc["buffers"] = [{"byteLength": len(self.data), "uri": uri}]


def box(centre, size, weights_low, weights_high):
    """A box's 24 vertices (flat normals), its 36 indices and each vertex's (joints, weights): vertices with y below
    the centre take `weights_low`, the others `weights_high`, each a list of (bone, weight)."""
    cx, cy, cz = centre
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (0, 0, 1), (1, 0, 0)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    positions, normals, skins, indices = [], [], [], []
    for normal, u, v in faces:
        first = len(positions)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = [normal[i] + su * u[i] + sv * v[i] for i in range(3)]
            point = (cx + p[0] * hx, cy + p[1] * hy, cz + p[2] * hz)
            positions.append(point)
            normals.append(normal)
            skins.append(weights_low if point[1] < cy else weights_high)
        indices += [first + i for i in (0, 1, 2, 0, 2, 3)]
    return positions, normals, skins, indices


def model(name, bones, parts, clips):
    """A glTF document: `bones` are (name, parent, rest translation); `parts` are box() results; `clips` are
    (name, {bone: (times, [(x, y, z, w)...])}, optional hips translation (times, [(x, y, z)...]))."""
    doc = {"asset": {"version": "2.0", "generator": "FenrirEngine AnimationDemo make_assets.py"}}
    buf = Buffer(doc)

    positions, normals, joints, weights, indices = [], [], [], [], []
    for part_positions, part_normals, part_skins, part_indices in parts:
        base = len(positions)
        positions += part_positions
        normals += part_normals
        for skin in part_skins:
            padded = list(skin) + [(0, 0.0)] * (4 - len(skin))
            joints.append(tuple(b for b, _ in padded))
            weights.append(tuple(w for _, w in padded))
        indices += [base + i for i in part_indices]
    primitive = {"attributes": {"POSITION": buf.floats(positions, "VEC3", bounds=True),
                                "NORMAL": buf.floats(normals, "VEC3"),
                                "JOINTS_0": buf.accessor(joints, 5123, "H", "VEC4", 34962),
                                "WEIGHTS_0": buf.floats(weights, "VEC4")},
                 "indices": buf.accessor([(i,) for i in indices], 5125, "I", "SCALAR", 34963),
                 "material": 0}
    doc["meshes"] = [{"name": name, "primitives": [primitive]}]
    doc["materials"] = [{"name": name + "Skin", "pbrMetallicRoughness": {
        "baseColorFactor": [0.75, 0.6, 0.45, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.8}}]

    # Nodes: 0 Armature, 1 the mesh node, then one per bone.
    model_positions = []
    for _, parent, rest in bones:
        base = model_positions[parent] if parent >= 0 else (0.0, 0.0, 0.0)
        model_positions.append(tuple(base[i] + rest[i] for i in range(3)))
    nodes = [{"name": "Armature", "children": [1, 2]}, {"name": name + "Mesh", "mesh": 0, "skin": 0}]
    for index, (bone_name, parent, rest) in enumerate(bones):
        node = {"name": bone_name, "translation": list(rest)}
        children = [2 + i for i, (_, p, _) in enumerate(bones) if p == index]
        if children:
            node["children"] = children
        nodes.append(node)
    doc["nodes"] = nodes
    inverse_binds = []
    for p in model_positions:
        # Column-major: the identity with the translation -p in the last column.
        inverse_binds.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1))
    doc["skins"] = [{"name": name + "Rig", "joints": [2 + i for i in range(len(bones))],
                     "inverseBindMatrices": buf.floats(inverse_binds, "MAT4", target=None)}]

    bone_index = {bone_name: i for i, (bone_name, _, _) in enumerate(bones)}
    animations = []
    for clip_name, rotations, bob in clips:
        samplers, channels = [], []
        for bone_name, (times, values) in rotations.items():
            samplers.append({"input": buf.floats([(t,) for t in times], "SCALAR", target=None, bounds=True),
                             "output": buf.floats(values, "VEC4", target=None), "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1,
                             "target": {"node": 2 + bone_index[bone_name], "path": "rotation"}})
        if bob is not None:
            times, values = bob
            samplers.append({"input": buf.floats([(t,) for t in times], "SCALAR", target=None, bounds=True),
                             "output": buf.floats(values, "VEC3", target=None), "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": 2, "path": "translation"}})
        animations.append({"name": clip_name, "samplers": samplers, "channels": channels})
    doc["animations"] = animations
    doc["scenes"] = [{"nodes": [0]}]
    doc["scene"] = 0
    buf.finish()
    return doc


def keyed(times, angles, axis):
    return (times, [quat(axis, a) for a in angles])


def mannequin():
    # (name, parent, rest translation from the parent): a figure about 2 units tall, facing +Z, arms hanging.
    bones = [
        ("Hips", -1, (0.0, 1.0, 0.0)), ("Spine", 0, (0.0, 0.2, 0.0)), ("Chest", 1, (0.0, 0.25, 0.0)),
        ("Neck", 2, (0.0, 0.25, 0.0)), ("Head", 3, (0.0, 0.1, 0.0)),
        ("UpperArm.L", 2, (0.22, 0.2, 0.0)), ("LowerArm.L", 5, (0.0, -0.3, 0.0)), ("Hand.L", 6, (0.0, -0.28, 0.0)),
        ("UpperArm.R", 2, (-0.22, 0.2, 0.0)), ("LowerArm.R", 8, (0.0, -0.3, 0.0)), ("Hand.R", 9, (0.0, -0.28, 0.0)),
        ("UpperLeg.L", 0, (0.1, -0.05, 0.0)), ("LowerLeg.L", 11, (0.0, -0.45, 0.0)), ("Foot.L", 12, (0.0, -0.45, 0.0)),
        ("UpperLeg.R", 0, (-0.1, -0.05, 0.0)), ("LowerLeg.R", 14, (0.0, -0.45, 0.0)), ("Foot.R", 15, (0.0, -0.45, 0.0)),
    ]
    B = {n: i for i, (n, _, _) in enumerate(bones)}

    def own(bone):
        return [(B[bone], 1.0)]

    def blend(a, b):
        return [(B[a], 0.5), (B[b], 0.5)]

    parts = [
        box((0, 1.0, 0), (0.34, 0.2, 0.2), own("Hips"), own("Hips")),
        box((0, 1.325, 0), (0.3, 0.25, 0.18), blend("Hips", "Spine"), blend("Spine", "Chest")),
        box((0, 1.575, 0), (0.4, 0.25, 0.22), own("Chest"), own("Chest")),
        box((0, 1.75, 0), (0.1, 0.1, 0.1), own("Neck"), own("Neck")),
        box((0, 1.93, 0.01), (0.24, 0.26, 0.24), own("Head"), own("Head")),
    ]
    for side, x in (("L", 0.22), ("R", -0.22)):
        parts += [
            box((x, 1.5, 0), (0.09, 0.3, 0.09), blend("UpperArm." + side, "LowerArm." + side), own("UpperArm." + side)),
            box((x, 1.22, 0), (0.08, 0.28, 0.08), blend("LowerArm." + side, "Hand." + side), blend("UpperArm." + side, "LowerArm." + side)),
            box((x, 1.01, 0), (0.07, 0.14, 0.1), own("Hand." + side), own("Hand." + side)),
        ]
    for side, x in (("L", 0.1), ("R", -0.1)):
        parts += [
            box((x, 0.725, 0), (0.13, 0.45, 0.13), blend("UpperLeg." + side, "LowerLeg." + side), own("UpperLeg." + side)),
            box((x, 0.275, 0), (0.11, 0.45, 0.11), own("LowerLeg." + side), blend("UpperLeg." + side, "LowerLeg." + side)),
            box((x, 0.03, 0.06), (0.11, 0.06, 0.24), own("Foot." + side), own("Foot." + side)),
        ]

    X, Z = (1, 0, 0), (0, 0, 1)
    idle = {"Chest": keyed([0, 1, 2], [0, 3, 0], X),
            "UpperArm.L": keyed([0, 1, 2], [4, 7, 4], Z), "UpperArm.R": keyed([0, 1, 2], [-4, -7, -4], Z),
            "Head": keyed([0, 1, 2], [0, -4, 0], X)}

    def gait(period, legs, knees, arms, elbows):
        t = [0, period / 4, period / 2, 3 * period / 4, period]
        return {
            "UpperLeg.L": keyed(t, [legs, 0, -legs, 0, legs], X), "UpperLeg.R": keyed(t, [-legs, 0, legs, 0, -legs], X),
            "LowerLeg.L": keyed(t, [0, knees, 0, 0, 0], X), "LowerLeg.R": keyed(t, [0, 0, 0, knees, 0], X),
            "UpperArm.L": keyed(t, [-arms, 0, arms, 0, -arms], X), "UpperArm.R": keyed(t, [arms, 0, -arms, 0, arms], X),
            "LowerArm.L": keyed(t, [-elbows] * 5, X), "LowerArm.R": keyed(t, [-elbows] * 5, X),
        }

    walk_bob = ([0, 0.25, 0.5, 0.75, 1.0], [(0, 1.0, 0), (0, 1.03, 0), (0, 1.0, 0), (0, 1.03, 0), (0, 1.0, 0)])
    run_bob = ([0, 0.15, 0.3, 0.45, 0.6], [(0, 0.95, 0), (0, 1.02, 0), (0, 0.95, 0), (0, 1.02, 0), (0, 0.95, 0)])
    raise_arm = quat(Z, -150)
    wave_times = [0, 0.3, 0.5, 0.7, 0.9, 1.1, 1.3, 1.6]
    wave = {"UpperArm.R": (wave_times, [quat(Z, 0)] + [raise_arm] * 6 + [quat(Z, 0)]),
            "LowerArm.R": keyed(wave_times, [0, 0, 25, -25, 25, -25, 0, 0], Z),
            "Head": keyed([0, 0.8, 1.6], [0, -8, 0], (0, 1, 0))}
    flutter = {"Hand.L": keyed([0, 0.125, 0.25], [0, 30, 0], X), "Hand.R": keyed([0, 0.125, 0.25], [0, -30, 0], X)}
    clips = [("Idle", idle, None), ("Walk", gait(1.0, 25, 35, 20, 10), walk_bob),
             ("Run", gait(0.6, 45, 70, 40, 70), run_bob), ("Wave", wave, None), ("Flutter", flutter, None)]
    return model("Mannequin", bones, parts, clips)


def critter():
    # A body on four legs, facing +Z, with a head forward and a five-segment tail behind.
    bones = [
        ("Body", -1, (0.0, 0.5, 0.0)), ("Head", 0, (0.0, 0.1, 0.35)),
        ("Leg.FL", 0, (0.12, -0.05, 0.22)), ("Leg.FR", 0, (-0.12, -0.05, 0.22)),
        ("Leg.BL", 0, (0.12, -0.05, -0.22)), ("Leg.BR", 0, (-0.12, -0.05, -0.22)),
        ("Tail1", 0, (0.0, 0.05, -0.3)), ("Tail2", 6, (0.0, 0.0, -0.15)), ("Tail3", 7, (0.0, 0.0, -0.15)),
        ("Tail4", 8, (0.0, 0.0, -0.15)), ("Tail5", 9, (0.0, 0.0, -0.15)),
    ]
    B = {n: i for i, (n, _, _) in enumerate(bones)}

    def own(bone):
        return [(B[bone], 1.0)]

    parts = [box((0, 0.5, 0), (0.3, 0.22, 0.6), own("Body"), own("Body")),
             box((0, 0.62, 0.42), (0.18, 0.16, 0.2), own("Head"), own("Head"))]
    for leg, x, z in (("FL", 0.12, 0.22), ("FR", -0.12, 0.22), ("BL", 0.12, -0.22), ("BR", -0.12, -0.22)):
        parts.append(box((x, 0.225, z), (0.07, 0.45, 0.07), own("Leg." + leg), own("Leg." + leg)))
    for segment in range(5):
        z = -0.3 - 0.15 * segment - 0.075
        parts.append(box((0, 0.55, z), (0.08 - 0.01 * segment, 0.08 - 0.01 * segment, 0.15),
                         own("Tail%d" % (segment + 1)), own("Tail%d" % (segment + 1))))

    Y = (0, 1, 0)
    t = [0, 0.25, 0.5, 0.75, 1.0]
    wag = {"Tail%d" % s: keyed(t, [0, 12 + 3 * s, 0, -12 - 3 * s, 0], Y) for s in range(1, 6)}
    wag["Head"] = keyed(t, [0, 5, 0, -5, 0], Y)
    return model("Critter", bones, parts, [("Wag", wag, None)])


def write(name, doc):
    with open(os.path.join(HERE, name), "w", newline="\n", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


if __name__ == "__main__":
    write("mannequin.gltf", mannequin())
    write("critter.gltf", critter())
