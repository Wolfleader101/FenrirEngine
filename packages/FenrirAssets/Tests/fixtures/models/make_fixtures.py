"""Writes FenrirAssets's model fixtures: one small scene as glTF, GLB, FBX (ASCII 7.4) and OBJ, plus the skinned,
missing-texture, occlusion-emissive, colour-space-conflict and corrupt glTF files, the skinned glTF fixtures of
ADR-0031's list (armatures, shared-skins, incompatible-skin, owners and cubic-clip) and its FBX ones (armatures,
shared-skins, incompatible-skin, units, baked-clip and clips-only .fbx), and the tangents .gltf and .fbx; each
function's docstring describes its file. Run by hand (`python make_fixtures.py`) from any directory; the files are
written beside this script and checked in. The build never runs it.

Everything here is written from scratch by this script, so the fixtures carry no third-party licence.

The scene (glTF's node order, which the importers keep: parents first):
  Root   T(1, 2, 3), R 90 degrees about +Y (written unnormalised in glTF), S(2, 2, 2)
    Panel  T(0, 1, 0), the mesh "Panel": a quad in the XY plane with normals and UVs, material "Painted"
    Marker T(0, 0, -1), the mesh "Marker": a triangle with no normals, material "Glow"
      Empty  T(0.5, 0, 0), no mesh
"Painted" is lit, blended, double-sided: base colour (0.8, 0.6, 0.4, 0.5) with checker.png (clamp U, repeat V,
nearest), metallic 0.25, roughness 0.75, emissive (0.1, 0.2, 0.3), normal map normal.png. "Glow" is unlit and masked
at 0.3 with base colour (0, 1, 0, 1). FBX and OBJ say what they can of that; the tests list the differences.
"""

import base64
import json
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def write(name, data):
    mode = "wb" if isinstance(data, (bytes, bytearray)) else "w"
    kwargs = {} if mode == "wb" else {"newline": "\n", "encoding": "utf-8"}
    with open(os.path.join(HERE, name), mode, **kwargs) as f:
        f.write(data)


def png(width, height, rgba_rows):
    """A minimal RGBA8 PNG from rows of (r, g, b, a) tuples, top row first."""

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in row) for row in rgba_rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


CHECKER = png(2, 2, [[(255, 255, 255, 255), (0, 0, 0, 255)], [(0, 0, 0, 255), (255, 255, 255, 255)]])
NORMAL = png(1, 1, [[(128, 128, 255, 255)]])

# The meshes, in glTF's conventions: UV (0, 0) is the image's top-left.
PANEL_POSITIONS = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
PANEL_NORMALS = [(0, 0, 1)] * 4
PANEL_UVS = [(0, 1), (1, 1), (1, 0), (0, 0)]
PANEL_INDICES = [0, 1, 2, 0, 2, 3]
MARKER_POSITIONS = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
MARKER_INDICES = [0, 1, 2]


class Buffer:
    """A glTF binary buffer, with accessors and buffer views appended to a glTF document."""

    def __init__(self, doc):
        self.doc = doc
        self.data = bytearray()
        doc.setdefault("bufferViews", [])
        doc.setdefault("accessors", [])

    def view(self, payload, target=None):
        while len(self.data) % 4:
            self.data.append(0)
        view = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target:
            view["target"] = target
        self.data += payload
        self.doc["bufferViews"].append(view)
        return len(self.doc["bufferViews"]) - 1

    def floats(self, rows, kind, target=34962):
        flat = [c for row in rows for c in row]
        view = self.view(struct.pack("<%df" % len(flat), *flat), target)
        accessor = {"bufferView": view, "componentType": 5126, "count": len(rows), "type": kind}
        if kind == "VEC3":
            accessor["min"] = [min(r[i] for r in rows) for i in range(3)]
            accessor["max"] = [max(r[i] for r in rows) for i in range(3)]
        if kind == "SCALAR":
            accessor["min"] = [min(r[0] for r in rows)]
            accessor["max"] = [max(r[0] for r in rows)]
        self.doc["accessors"].append(accessor)
        return len(self.doc["accessors"]) - 1

    def indices(self, values):
        view = self.view(struct.pack("<%dI" % len(values), *values), 34963)
        self.doc["accessors"].append({"bufferView": view, "componentType": 5125, "count": len(values), "type": "SCALAR"})
        return len(self.doc["accessors"]) - 1

    def bytes_accessor(self, rows, component_type, fmt, kind):
        flat = [c for row in rows for c in row]
        view = self.view(struct.pack("<%d%s" % (len(flat), fmt), *flat), 34962)
        self.doc["accessors"].append({"bufferView": view, "componentType": component_type, "count": len(rows), "type": kind})
        return len(self.doc["accessors"]) - 1


def scene_document(embed_images):
    doc = {"asset": {"version": "2.0", "generator": "FenrirEngine make_fixtures.py"},
           "extensionsUsed": ["KHR_materials_unlit"]}
    buf = Buffer(doc)
    panel = {"attributes": {"POSITION": buf.floats(PANEL_POSITIONS, "VEC3"), "NORMAL": buf.floats(PANEL_NORMALS, "VEC3"),
                            "TEXCOORD_0": buf.floats(PANEL_UVS, "VEC2")},
             "indices": buf.indices(PANEL_INDICES), "material": 0}
    marker = {"attributes": {"POSITION": buf.floats(MARKER_POSITIONS, "VEC3")},
              "indices": buf.indices(MARKER_INDICES), "material": 1}
    doc["meshes"] = [{"name": "Panel", "primitives": [panel]}, {"name": "Marker", "primitives": [marker]}]
    if embed_images:
        doc["images"] = [{"name": "checker", "mimeType": "image/png", "bufferView": buf.view(CHECKER)},
                         {"name": "normal", "mimeType": "image/png", "bufferView": buf.view(NORMAL)}]
    else:
        doc["images"] = [{"name": "checker", "uri": "checker.png"}, {"name": "normal", "uri": "normal.png"}]
    # 33071 clamp to edge, 10497 repeat, 9728 nearest.
    doc["samplers"] = [{"wrapS": 33071, "wrapT": 10497, "magFilter": 9728, "minFilter": 9728}]
    doc["textures"] = [{"source": 0, "sampler": 0}, {"source": 1}]
    doc["materials"] = [
        {"name": "Painted", "alphaMode": "BLEND", "doubleSided": True,
         "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.6, 0.4, 0.5], "baseColorTexture": {"index": 0},
                                  "metallicFactor": 0.25, "roughnessFactor": 0.75},
         "normalTexture": {"index": 1}, "emissiveFactor": [0.1, 0.2, 0.3]},
        {"name": "Glow", "alphaMode": "MASK", "alphaCutoff": 0.3,
         "pbrMetallicRoughness": {"baseColorFactor": [0, 1, 0, 1]}, "extensions": {"KHR_materials_unlit": {}}},
    ]
    doc["nodes"] = [
        {"name": "Root", "translation": [1, 2, 3], "rotation": [0, 1, 0, 1], "scale": [2, 2, 2], "children": [1, 2]},
        {"name": "Panel", "translation": [0, 1, 0], "mesh": 0},
        {"name": "Marker", "translation": [0, 0, -1], "mesh": 1, "children": [3]},
        {"name": "Empty", "translation": [0.5, 0, 0]},
    ]
    doc["scenes"] = [{"nodes": [0]}]
    doc["scene"] = 0
    return doc, buf


def write_gltf(name, doc, buf):
    bin_name = name.replace(".gltf", ".bin")
    doc["buffers"] = [{"uri": bin_name, "byteLength": len(buf.data)}]
    write(bin_name, bytes(buf.data))
    write(name, json.dumps(doc, indent=2) + "\n")


def write_glb(name, doc, buf):
    while len(buf.data) % 4:
        buf.data.append(0)
    doc["buffers"] = [{"byteLength": len(buf.data)}]
    text = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    body = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(buf.data), 0x004E4942) + buf.data
    write(name, struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body)


def write_obj():
    lines = ["# FenrirAssets model fixture: the scene's two meshes, written by make_fixtures.py.",
             "# OBJ has no hierarchy or node transforms, so each mesh is an object at the origin.",
             "mtllib scene.mtl"]
    # OBJ's UV origin is the image's bottom-left.
    lines.append("o Panel")
    lines += ["v %g %g %g" % p for p in PANEL_POSITIONS]
    lines += ["vt %g %g" % (u, 1 - v) for u, v in PANEL_UVS]
    lines += ["vn %g %g %g" % n for n in PANEL_NORMALS]
    lines.append("usemtl Painted")
    lines.append("f 1/1/1 2/2/2 3/3/3")
    lines.append("f 1/1/1 3/3/3 4/4/4")
    lines.append("o Marker")
    lines += ["v %g %g %g" % p for p in MARKER_POSITIONS]
    lines.append("usemtl Glow")
    lines.append("f 5 6 7")
    write("scene.obj", "\n".join(lines) + "\n")
    mtl = ["# The scene's materials, as far as MTL can say them: no unlit, mask or double-sidedness.",
           "newmtl Painted", "Kd 0.8 0.6 0.4", "d 0.5", "Ke 0.1 0.2 0.3", "Pm 0.25", "Pr 0.75",
           "map_Kd checker.png", "norm normal.png",
           "newmtl Glow", "Kd 0 1 0"]
    write("scene.mtl", "\n".join(mtl) + "\n")


def fbx_array(name, values, kind="a"):
    text = ",".join("%.12g" % v for v in values)
    return "\t\t%s: *%d {\n\t\t\t%s: %s\n\t\t}\n" % (name, len(values), kind, text)


def fbx_model(uid, name, kind, translation=None, rotation=None, scale=None):
    props = []
    if translation:
        props.append('\t\t\tP: "Lcl Translation", "Lcl Translation", "", "A",%g,%g,%g' % translation)
    if rotation:
        props.append('\t\t\tP: "Lcl Rotation", "Lcl Rotation", "", "A",%g,%g,%g' % rotation)
    if scale:
        props.append('\t\t\tP: "Lcl Scaling", "Lcl Scaling", "", "A",%g,%g,%g' % scale)
    return ('\tModel: %d, "Model::%s", "%s" {\n\t\tVersion: 232\n\t\tProperties70:  {\n%s\n\t\t}\n'
            '\t\tShading: T\n\t\tCulling: "CullingOff"\n\t}\n' % (uid, name, kind, "\n".join(props)))


def fbx_geometry(uid, name, positions, polygon, normals=None, uvs=None, tangents=None, binormals=None):
    # A polygon's last index is written as -(index + 1).
    indices = list(polygon)
    flat = [c for p in positions for c in p]
    out = '\tGeometry: %d, "Geometry::%s", "Mesh" {\n' % (uid, name)
    out += fbx_array("Vertices", flat)
    out += fbx_array("PolygonVertexIndex", indices)
    out += "\t\tGeometryVersion: 124\n"
    layers = []
    corners = [i if i >= 0 else -i - 1 for i in indices]
    if normals:
        out += ('\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
                '\t\t\tMappingInformationType: "ByPolygonVertex"\n\t\t\tReferenceInformationType: "Direct"\n')
        out += fbx_array("Normals", [c for i in corners for c in normals[i]]).replace("\t\t", "\t\t\t", 1)
        out += "\t\t}\n"
        layers.append("LayerElementNormal")
    for element, array, values in (("LayerElementTangent", "Tangents", tangents),
                                   ("LayerElementBinormal", "Binormals", binormals)):
        if values:
            out += ('\t\t%s: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
                    '\t\t\tMappingInformationType: "ByPolygonVertex"\n\t\t\tReferenceInformationType: "Direct"\n'
                    % element)
            out += fbx_array(array, [c for i in corners for c in values[i]]).replace("\t\t", "\t\t\t", 1)
            out += "\t\t}\n"
            layers.append(element)
    if uvs:
        out += ('\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: "UVMap"\n'
                '\t\t\tMappingInformationType: "ByPolygonVertex"\n\t\t\tReferenceInformationType: "IndexToDirect"\n')
        # FBX's UV origin is the image's bottom-left.
        out += fbx_array("UV", [c for u, v in uvs for c in (u, 1 - v)]).replace("\t\t", "\t\t\t", 1)
        out += fbx_array("UVIndex", corners).replace("\t\t", "\t\t\t", 1)
        out += "\t\t}\n"
        layers.append("LayerElementUV")
    out += ('\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
            '\t\t\tMappingInformationType: "AllSame"\n\t\t\tReferenceInformationType: "IndexToDirect"\n'
            '\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n')
    layers.append("LayerElementMaterial")
    out += "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
    for layer in layers:
        out += '\t\t\tLayerElement:  {\n\t\t\t\tType: "%s"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n' % layer
    out += "\t\t}\n\t}\n"
    return out


def fbx_material(uid, name, props):
    body = "\n".join('\t\t\tP: "%s", "%s", "", "A",%s' % (n, t, ",".join("%g" % v for v in vals)) for n, t, vals in props)
    return ('\tMaterial: %d, "Material::%s", "" {\n\t\tVersion: 102\n\t\tShadingModel: "phong"\n\t\tMultiLayer: 0\n'
            '\t\tProperties70:  {\n%s\n\t\t}\n\t}\n' % (uid, name, body))


def fbx_texture(uid, name, filename, wrap_u, wrap_v):
    return ('\tTexture: %d, "Texture::%s", "" {\n\t\tType: "TextureVideoClip"\n\t\tVersion: 202\n'
            '\t\tTextureName: "Texture::%s"\n\t\tProperties70:  {\n'
            '\t\t\tP: "WrapModeU", "enum", "", "",%d\n\t\t\tP: "WrapModeV", "enum", "", "",%d\n\t\t}\n'
            '\t\tFileName: "%s"\n\t\tRelativeFilename: "%s"\n\t}\n' % (uid, name, name, wrap_u, wrap_v, filename, filename))


def write_fbx():
    header = ("; FBX 7.4.0 project file\n"
              "; FenrirAssets model fixture, written by hand-rolled make_fixtures.py (no FBX SDK involved).\n"
              "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n"
              '\tCreator: "FenrirEngine make_fixtures.py"\n}\n'
              "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
              '\t\tP: "UpAxis", "int", "Integer", "",1\n\t\tP: "UpAxisSign", "int", "Integer", "",1\n'
              '\t\tP: "FrontAxis", "int", "Integer", "",2\n\t\tP: "FrontAxisSign", "int", "Integer", "",1\n'
              '\t\tP: "CoordAxis", "int", "Integer", "",0\n\t\tP: "CoordAxisSign", "int", "Integer", "",1\n'
              '\t\tP: "UnitScaleFactor", "double", "Number", "",100\n'
              '\t\tP: "OriginalUnitScaleFactor", "double", "Number", "",100\n\t}\n}\n')
    objects = "Objects:  {\n"
    objects += fbx_model(1001, "Root", "Null", (1, 2, 3), (0, 90, 0), (2, 2, 2))
    objects += fbx_model(1002, "Panel", "Mesh", (0, 1, 0))
    objects += fbx_model(1003, "Marker", "Mesh", (0, 0, -1))
    objects += fbx_model(1004, "Empty", "Null", (0.5, 0, 0))
    objects += fbx_geometry(2001, "Panel", PANEL_POSITIONS, [0, 1, 2, -4], PANEL_NORMALS, PANEL_UVS)
    objects += fbx_geometry(2002, "Marker", MARKER_POSITIONS, [0, 1, -3])
    objects += fbx_material(3001, "Painted", [("DiffuseColor", "Color", (0.8, 0.6, 0.4)),
                                              ("DiffuseFactor", "Number", (1,)),
                                              ("EmissiveColor", "Color", (0.1, 0.2, 0.3)),
                                              ("EmissiveFactor", "Number", (1,)),
                                              ("TransparencyFactor", "Number", (0.5,))])
    objects += fbx_material(3002, "Glow", [("DiffuseColor", "Color", (0, 1, 0)), ("DiffuseFactor", "Number", (1,))])
    # WrapMode 0 is repeat, 1 clamp.
    objects += fbx_texture(4001, "checker", "checker.png", 1, 0)
    objects += fbx_texture(4002, "normal", "normal.png", 0, 0)
    objects += "}\n"
    connections = ("Connections:  {\n"
                   '\tC: "OO",1001,0\n\tC: "OO",1002,1001\n\tC: "OO",1003,1001\n\tC: "OO",1004,1003\n'
                   '\tC: "OO",2001,1002\n\tC: "OO",3001,1002\n\tC: "OO",2002,1003\n\tC: "OO",3002,1003\n'
                   '\tC: "OP",4001,3001, "DiffuseColor"\n\tC: "OP",4002,3001, "NormalMap"\n}\n')
    write("scene.fbx", header + objects + connections)


def write_skinned():
    """Rig > Bone (a joint), Body (a skinned mesh, SkinnedBody) and Prop (a static mesh)."""
    doc = {"asset": {"version": "2.0", "generator": "FenrirEngine make_fixtures.py"}}
    buf = Buffer(doc)
    body = {"attributes": {"POSITION": buf.floats(MARKER_POSITIONS, "VEC3"),
                           "JOINTS_0": buf.bytes_accessor([(0, 0, 0, 0)] * 3, 5121, "B", "VEC4"),
                           "WEIGHTS_0": buf.floats([(1, 0, 0, 0)] * 3, "VEC4")},
            "indices": buf.indices(MARKER_INDICES)}
    prop = {"attributes": {"POSITION": buf.floats(MARKER_POSITIONS, "VEC3")}, "indices": buf.indices(MARKER_INDICES)}
    doc["meshes"] = [{"name": "SkinnedBody", "primitives": [body]}, {"name": "Prop", "primitives": [prop]}]
    doc["skins"] = [{"joints": [1]}]
    doc["nodes"] = [{"name": "Rig", "children": [1, 2, 3]}, {"name": "Bone", "translation": [0, 1, 0]},
                    {"name": "Body", "mesh": 0, "skin": 0}, {"name": "Prop", "mesh": 1}]
    doc["scenes"] = [{"nodes": [0]}]
    doc["scene"] = 0
    write_gltf("skinned.gltf", doc, buf)


# Skinned fixtures (ADR-0031's glTF list). Matrices are 16 floats, column-major as glTF and glm store them.

def quat(axis, degrees):
    """The unit quaternion (x, y, z, w) turning `degrees` about `axis`."""
    length = math.sqrt(sum(a * a for a in axis))
    half = math.radians(degrees) / 2
    s = math.sin(half) / length
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def trs(t=(0, 0, 0), r=(0, 0, 0, 1), s=(1, 1, 1)):
    x, y, z, w = r
    rot = [[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
           [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
           [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]]
    m = []
    for c in range(3):
        m += [rot[c][0] * s[c], rot[c][1] * s[c], rot[c][2] * s[c], 0.0]
    return m + [t[0], t[1], t[2], 1.0]


def mul(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def inverse(m):
    rows = [[m[c * 4 + r] for c in range(4)] + [1.0 if r == i else 0.0 for i in range(4)] for r in range(4)]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda r: abs(rows[r][col]))
        rows[col], rows[pivot] = rows[pivot], rows[col]
        p = rows[col][col]
        rows[col] = [v / p for v in rows[col]]
        for r in range(4):
            if r != col:
                f = rows[r][col]
                rows[r] = [v - f * w for v, w in zip(rows[r], rows[col])]
    return [rows[r][4 + c] for c in range(4) for r in range(4)]


def transform_point(m, p):
    return tuple(m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] for r in range(3))


class Rig:
    """A glTF document's nodes, with each node's rest world for inverse binds."""

    def __init__(self, doc):
        self.doc = doc
        doc.setdefault("nodes", [])

    def node(self, name, parent=None, t=None, r=None, s=None, **extra):
        node = {"name": name}
        if t:
            node["translation"] = list(t)
        if r:
            node["rotation"] = list(r)
        if s:
            node["scale"] = list(s)
        node.update(extra)
        index = len(self.doc["nodes"])
        self.doc["nodes"].append(node)
        node["_parent"] = parent
        if parent is not None:
            self.doc["nodes"][parent].setdefault("children", []).append(index)
        return index

    def world(self, index):
        node = self.doc["nodes"][index]
        local = trs(node.get("translation", (0, 0, 0)), node.get("rotation", (0, 0, 0, 1)), node.get("scale", (1, 1, 1)))
        parent = node["_parent"]
        return local if parent is None else mul(self.world(parent), local)

    def finish(self):
        for node in self.doc["nodes"]:
            del node["_parent"]


def skin(doc, buf, name, joints, inverse_binds, skeleton=None):
    entry = {"name": name, "joints": joints,
             "inverseBindMatrices": buf.floats(inverse_binds, "MAT4", None)}
    if skeleton is not None:
        entry["skeleton"] = skeleton
    doc.setdefault("skins", []).append(entry)
    return len(doc["skins"]) - 1


def skinned_mesh(doc, buf, name, positions, joints, weights, normals=None, tangents=None, indices=None):
    attributes = {"POSITION": buf.floats(positions, "VEC3"),
                  "JOINTS_0": buf.bytes_accessor(joints, 5121, "B", "VEC4"),
                  "WEIGHTS_0": buf.floats(weights, "VEC4")}
    if normals:
        attributes["NORMAL"] = buf.floats(normals, "VEC3")
    if tangents:
        attributes["TANGENT"] = buf.floats(tangents, "VEC4")
    doc.setdefault("meshes", []).append(
        {"name": name, "primitives": [{"attributes": attributes, "indices": buf.indices(indices or list(range(len(positions))))}]})
    return len(doc["meshes"]) - 1


def rigid_mesh(doc, buf, name):
    doc.setdefault("meshes", []).append(
        {"name": name, "primitives": [{"attributes": {"POSITION": buf.floats(MARKER_POSITIONS, "VEC3")},
                                       "indices": buf.indices(MARKER_INDICES)}]})
    return len(doc["meshes"]) - 1


def channel(doc, buf, animation, node, path, times, values, interpolation="LINEAR"):
    sampler = {"input": buf.floats([(t,) for t in times], "SCALAR", None),
               "output": buf.floats(values, "VEC4" if path == "rotation" else "VEC3", None),
               "interpolation": interpolation}
    animation["samplers"].append(sampler)
    animation["channels"].append({"sampler": len(animation["samplers"]) - 1, "target": {"node": node, "path": path}})


def new_document():
    doc = {"asset": {"version": "2.0", "generator": "FenrirEngine make_fixtures.py"}}
    return doc, Buffer(doc)


def write_armatures():
    """Two unrelated armatures under Scene, a third under Rig, and the bone rules (ADR-0031).

    Scene
      Hips   joint of HumanSkin, T(0, 1, 0)
        Spine  no joint, between Hips and Chest: an intermediate bone, T(0, 0.5, 0)
          Chest  joint, T(0, 0.5, 0)
            Head    joint no vertex is weighted to: an unweighted leaf, T(0, 0.5, 0)
            Socket  an empty child of a bone: an ordinary node, T(0.2, 0, 0)
            Hat     a mesh child of a bone, animated: still ordinary, T(0, 0.6, 0)
            Tail    an empty child of a bone, animated: a bone, T(0, 0, -0.3)
      Pelvis  joint of TwinSkin, T(3, 1, 0)
        Pelvis  joint, a repeated name, T(0, 0.5, 0)
      Rig    T(-3, 0, 0)
        Root   joint of PropSkin, T(0, 1, 0)
      BodyA, BodyB  two meshes skinned with HumanSkin (one skeleton shared)
      Legs   skinned with TwinSkin; Prop skinned with PropSkin
    HumanSkin names Chest, a joint, as its skeleton, so it names no owner. The rest pose is the bind pose. The clip
    "Wave" keys Spine and Tail rotations and Hat's translation.
    """
    doc, buf = new_document()
    rig = Rig(doc)
    scene = rig.node("Scene")
    hips = rig.node("Hips", scene, t=(0, 1, 0))
    spine = rig.node("Spine", hips, t=(0, 0.5, 0))
    chest = rig.node("Chest", spine, t=(0, 0.5, 0))
    head = rig.node("Head", chest, t=(0, 0.5, 0))
    rig.node("Socket", chest, t=(0.2, 0, 0))
    hat = rig.node("Hat", chest, t=(0, 0.6, 0), mesh=rigid_mesh(doc, buf, "HatMesh"))
    tail = rig.node("Tail", chest, t=(0, 0, -0.3))
    pelvis = rig.node("Pelvis", scene, t=(3, 1, 0))
    pelvis2 = rig.node("Pelvis", pelvis, t=(0, 0.5, 0))
    holder = rig.node("Rig", scene, t=(-3, 0, 0))
    root = rig.node("Root", holder, t=(0, 1, 0))

    human = skin(doc, buf, "HumanSkin", [hips, chest, head], [inverse(rig.world(j)) for j in (hips, chest, head)],
                 skeleton=chest)
    twin = skin(doc, buf, "TwinSkin", [pelvis, pelvis2], [inverse(rig.world(j)) for j in (pelvis, pelvis2)])
    prop = skin(doc, buf, "PropSkin", [root], [inverse(rig.world(root))])

    body_a = skinned_mesh(doc, buf, "BodyA", [(0, 1, 0), (0.5, 1.5, 0), (0, 2, 0)],
                          [(0, 1, 0, 0), (0, 1, 0, 0), (1, 0, 0, 0)], [(1, 0, 0, 0), (0.5, 0.5, 0, 0), (1, 0, 0, 0)])
    body_b = skinned_mesh(doc, buf, "BodyB", [(0, 2, 0), (0.5, 2, 0), (0, 2.5, 0)],
                          [(1, 0, 0, 0)] * 3, [(1, 0, 0, 0)] * 3)
    legs = skinned_mesh(doc, buf, "Legs", [(3, 1, 0), (3.5, 1, 0), (3, 1.5, 0)],
                        [(0, 1, 0, 0), (0, 0, 0, 0), (1, 0, 0, 0)], [(0.25, 0.75, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)])
    prop_mesh = skinned_mesh(doc, buf, "Prop", [(-3, 1, 0), (-2, 1, 0), (-3, 2, 0)], [(0, 0, 0, 0)] * 3,
                             [(1, 0, 0, 0)] * 3)
    rig.node("BodyA", scene, mesh=body_a, skin=human)
    rig.node("BodyB", scene, mesh=body_b, skin=human)
    rig.node("Legs", scene, mesh=legs, skin=twin)
    rig.node("Prop", scene, mesh=prop_mesh, skin=prop)

    wave = {"name": "Wave", "samplers": [], "channels": []}
    channel(doc, buf, wave, spine, "rotation", [0, 1], [(0, 0, 0, 1), quat((0, 0, 1), 30)])
    channel(doc, buf, wave, tail, "rotation", [0, 1], [(0, 0, 0, 1), quat((1, 0, 0), 45)])
    channel(doc, buf, wave, hat, "translation", [0, 1], [(0, 0.6, 0), (0, 0.8, 0)])
    doc["animations"] = [wave]
    rig.finish()
    doc["scenes"] = [{"nodes": [scene]}]
    doc["scene"] = 0
    write_gltf("armatures.gltf", doc, buf)


def write_shared_skins():
    """Three skins on one leg (Rig > Hip > Knee > Foot, each T(0, 1, 0), (0, -0.5, 0), (0, -0.5, 0)).

    ThighSkin [Hip, Knee] and ShinSkin [Knee, Foot] have equal binds, so they merge. BootSkin [Foot, Knee] has binds
    B x C with C = T(0, 0, 2): its vertices are written at bind positions minus (0, 0, 2), and one conversion takes
    them into the others'. Its joints come in another order. The rest pose is the bind pose.
    """
    doc, buf = new_document()
    rig = Rig(doc)
    holder = rig.node("Rig")
    hip = rig.node("Hip", holder, t=(0, 1, 0))
    knee = rig.node("Knee", hip, t=(0, -0.5, 0))
    foot = rig.node("Foot", knee, t=(0, -0.5, 0))
    ibm = {j: inverse(rig.world(j)) for j in (hip, knee, foot)}
    conversion = trs(t=(0, 0, 2))
    thigh = skin(doc, buf, "ThighSkin", [hip, knee], [ibm[hip], ibm[knee]])
    shin = skin(doc, buf, "ShinSkin", [knee, foot], [ibm[knee], ibm[foot]])
    boot = skin(doc, buf, "BootSkin", [foot, knee], [mul(ibm[foot], conversion), mul(ibm[knee], conversion)])
    thigh_mesh = skinned_mesh(doc, buf, "Thigh", [(0, 1, 0), (0.2, 0.75, 0), (0, 0.5, 0)],
                              [(0, 0, 0, 0), (0, 1, 0, 0), (1, 0, 0, 0)], [(1, 0, 0, 0), (0.5, 0.5, 0, 0), (1, 0, 0, 0)])
    shin_mesh = skinned_mesh(doc, buf, "Shin", [(0, 0.5, 0), (0.2, 0.25, 0), (0, 0, 0)],
                             [(0, 0, 0, 0), (0, 1, 0, 0), (1, 0, 0, 0)], [(1, 0, 0, 0), (0.5, 0.5, 0, 0), (1, 0, 0, 0)])
    boot_mesh = skinned_mesh(doc, buf, "Boot", [(0, 0, -2), (0.3, 0, -2), (0, 0.2, -2)],
                             [(0, 0, 0, 0), (0, 0, 0, 0), (0, 1, 0, 0)], [(1, 0, 0, 0), (1, 0, 0, 0), (0.5, 0.5, 0, 0)])
    rig.node("Thigh", holder, mesh=thigh_mesh, skin=thigh)
    rig.node("Shin", holder, mesh=shin_mesh, skin=shin)
    rig.node("Boot", holder, mesh=boot_mesh, skin=boot)
    rig.finish()
    doc["scenes"] = [{"nodes": [holder]}]
    doc["scene"] = 0
    write_gltf("shared-skins.gltf", doc, buf)


def write_incompatible_skin():
    """Rig > Arm > Hand, with SleeveSkin [Arm, Hand] and GloveSkin [Hand, Arm], whose binds are SleeveSkin's moved
    by T(1, 0, 0) for Hand and T(0, 1, 0) for Arm: no one conversion takes them into SleeveSkin's, so GloveSkin fails.
    Other > Bone, with CloakSkin naming Bone twice as a joint: its skeleton fails. Badge is a rigid mesh on Rig.
    """
    doc, buf = new_document()
    rig = Rig(doc)
    holder = rig.node("Rig", mesh=rigid_mesh(doc, buf, "Badge"))
    arm = rig.node("Arm", holder, t=(0, 1, 0))
    hand = rig.node("Hand", arm, t=(1, 0, 0))
    other = rig.node("Other", t=(0, 0, 5))
    bone = rig.node("Bone", other, t=(0, 1, 0))
    ibm = {j: inverse(rig.world(j)) for j in (arm, hand, bone)}
    sleeve = skin(doc, buf, "SleeveSkin", [arm, hand], [ibm[arm], ibm[hand]])
    glove = skin(doc, buf, "GloveSkin", [hand, arm], [mul(ibm[hand], trs(t=(1, 0, 0))), mul(ibm[arm], trs(t=(0, 1, 0)))])
    cloak = skin(doc, buf, "CloakSkin", [bone, bone], [ibm[bone], ibm[bone]])
    triangle = [(0, 1, 0), (1, 1, 0), (0, 2, 0)]
    rig.node("Sleeve", holder, mesh=skinned_mesh(doc, buf, "Sleeve", triangle, [(0, 0, 0, 0)] * 3,
                                                 [(1, 0, 0, 0)] * 3), skin=sleeve)
    rig.node("Glove", holder, mesh=skinned_mesh(doc, buf, "Glove", triangle, [(0, 0, 0, 0)] * 3,
                                                [(1, 0, 0, 0)] * 3), skin=glove)
    rig.node("Cloak", other, mesh=skinned_mesh(doc, buf, "Cloak", triangle, [(0, 0, 0, 0)] * 3,
                                               [(1, 0, 0, 0)] * 3), skin=cloak)
    rig.finish()
    doc["scenes"] = [{"nodes": [holder, other]}]
    doc["scene"] = 0
    write_gltf("incompatible-skin.gltf", doc, buf)


# owners.gltf's numbers, which the numerical owner test repeats.
OWNER_T, OWNER_R, OWNER_S = (1, 2, 3), quat((1, 1, 0), 30), (2, 0.5, 1.5)
UPPER_REST = ((0, 1, 0), quat((0, 0, 1), 20))
LOWER_REST = ((0, 1, 0), quat((1, 0, 0), -35))
SLEEVE_POSITIONS = [(0.1, 1.0, 0.0), (0.0, 1.5, 0.2), (-0.1, 2.0, 0.0), (0.2, 2.5, -0.1)]
SLEEVE_JOINTS = [(0, 1, 0, 0), (0, 1, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)]
SLEEVE_WEIGHTS = [(0.7, 0.3, 0, 0), (0.5, 0.5, 0, 0), (0.6, 0.4, 0, 0), (1, 0, 0, 0)]


def write_owners():
    """Two skinned meshes under owners that are not the identity.

    Owner  T(1, 2, 3), R 30 degrees about (1, 1, 0), S(2, 0.5, 1.5)
      Upper  joint, rest T(0, 1, 0) R 20 degrees about +Z
        Lower  joint, rest T(0, 1, 0) R -35 degrees about +X
      Sleeve  skinned with ArmSkin, several influences per vertex, a normal and a tangent each
    Mirror  S(-1, 1, 1): a negative determinant
      Bone  joint, T(0, 1, 0)
      Mirrored  skinned with MirrorSkin
    ArmSkin's inverse binds are of the bind pose with Upper and Lower unrotated (so rest and bind differ); MirrorSkin's
    are of its rest pose. Vertices, normals and tangents are glTF's, in scene-root space.
    """
    doc, buf = new_document()
    rig = Rig(doc)
    owner = rig.node("Owner", t=OWNER_T, r=OWNER_R, s=OWNER_S)
    upper = rig.node("Upper", owner, t=UPPER_REST[0], r=UPPER_REST[1])
    lower = rig.node("Lower", upper, t=LOWER_REST[0], r=LOWER_REST[1])
    owner_world = rig.world(owner)
    upper_bind = mul(owner_world, trs(t=UPPER_REST[0]))
    lower_bind = mul(upper_bind, trs(t=LOWER_REST[0]))
    arm = skin(doc, buf, "ArmSkin", [upper, lower], [inverse(upper_bind), inverse(lower_bind)])
    positions = [transform_point(owner_world, p) for p in SLEEVE_POSITIONS]
    normals = [(1, 0, 0), (0, 0, 1), (0.6, 0.8, 0), (0, 1, 0)]
    tangents = [(0, 1, 0, 1), (1, 0, 0, 1), (0.8, -0.6, 0, -1), (0, 0, 1, 1)]
    sleeve = skinned_mesh(doc, buf, "Sleeve", positions, SLEEVE_JOINTS, SLEEVE_WEIGHTS, normals, tangents,
                          [0, 1, 2, 1, 3, 2])
    rig.node("Sleeve", owner, mesh=sleeve, skin=arm)

    mirror = rig.node("Mirror", s=(-1, 1, 1))
    bone = rig.node("Bone", mirror, t=(0, 1, 0))
    mirror_skin = skin(doc, buf, "MirrorSkin", [bone], [inverse(rig.world(bone))])
    mirrored = skinned_mesh(doc, buf, "Mirrored", [(-1, 1, 0), (-2, 1, 0), (-1, 2, 0)], [(0, 0, 0, 0)] * 3,
                            [(1, 0, 0, 0)] * 3, [(0, 0, 1)] * 3, [(-1, 0, 0, 1)] * 3)
    rig.node("Mirrored", mirror, mesh=mirrored, skin=mirror_skin)
    rig.finish()
    doc["scenes"] = [{"nodes": [owner, mirror]}]
    doc["scene"] = 0
    write_gltf("owners.gltf", doc, buf)


def write_cubic_clip():
    """Rig > Joint, with Body skinned to it, and the clip "Turn": a cubic rotation of Joint over keys at 0, 1 and 2 s.

    Key 1's value (0, 0, -0.3826834, -0.9238795) is 45 degrees about +Z in the far hemisphere from key 0's identity,
    with in tangent (0, 0, 0.1, 0.2) and out tangent (0, 0, -0.1, 0.3); key 2 is 90 degrees about +Z, near key 1 once
    key 1 is flipped (but not as written). Keys 0 and 2 have tangents (0, 0, 0.05, 0) in and (0, 0, 0.02, 0) out.
    """
    doc, buf = new_document()
    rig = Rig(doc)
    holder = rig.node("Rig")
    joint = rig.node("Joint", holder, t=(0, 1, 0))
    body_skin = skin(doc, buf, "BodySkin", [joint], [inverse(rig.world(joint))])
    body = skinned_mesh(doc, buf, "Body", [(0, 1, 0), (1, 1, 0), (0, 2, 0)], [(0, 0, 0, 0)] * 3, [(1, 0, 0, 0)] * 3)
    rig.node("Body", holder, mesh=body, skin=body_skin)
    turn = {"name": "Turn", "samplers": [], "channels": []}
    edge_in, edge_out = (0, 0, 0.05, 0), (0, 0, 0.02, 0)
    values = [edge_in, (0, 0, 0, 1), edge_out,
              (0, 0, 0.1, 0.2), (0, 0, -0.3826834, -0.9238795), (0, 0, -0.1, 0.3),
              edge_in, (0, 0, 0.7071068, 0.7071068), edge_out]
    channel(doc, buf, turn, joint, "rotation", [0, 1, 2], values, "CUBICSPLINE")
    doc["animations"] = [turn]
    rig.finish()
    doc["scenes"] = [{"nodes": [holder]}]
    doc["scene"] = 0
    write_gltf("cubic-clip.gltf", doc, buf)


def write_missing_texture():
    """One triangle whose material's base colour texture names a file that is not there."""
    doc = {"asset": {"version": "2.0", "generator": "FenrirEngine make_fixtures.py"}}
    buf = Buffer(doc)
    tri = {"attributes": {"POSITION": buf.floats(MARKER_POSITIONS, "VEC3")}, "indices": buf.indices(MARKER_INDICES),
           "material": 0}
    doc["meshes"] = [{"name": "Triangle", "primitives": [tri]}]
    doc["images"] = [{"uri": "absent.png"}]
    doc["textures"] = [{"source": 0}]
    doc["materials"] = [{"name": "Lost", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}]
    doc["nodes"] = [{"name": "Triangle", "mesh": 0}]
    doc["scenes"] = [{"nodes": [0]}]
    doc["scene"] = 0
    write_gltf("missing-texture.gltf", doc, buf)


def write_occlusion_emissive():
    """One triangle whose material has an occlusion texture (the checker, embedded) and an emissive strength of 4."""
    doc = {"asset": {"version": "2.0", "generator": "FenrirEngine make_fixtures.py"},
           "extensionsUsed": ["KHR_materials_emissive_strength"]}
    buf = Buffer(doc)
    uv = buf.floats([(0, 0), (1, 0), (0, 1)], "VEC2")
    tri = {"attributes": {"POSITION": buf.floats(MARKER_POSITIONS, "VEC3"), "TEXCOORD_0": uv},
           "indices": buf.indices(MARKER_INDICES), "material": 0}
    doc["meshes"] = [{"name": "Triangle", "primitives": [tri]}]
    doc["images"] = [{"uri": "data:image/png;base64," + base64.b64encode(CHECKER).decode("ascii")}]
    doc["textures"] = [{"source": 0}]
    doc["materials"] = [{"name": "Shaded", "occlusionTexture": {"index": 0}, "emissiveFactor": [0.5, 0.25, 1.0],
                         "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 4.0}}}]
    doc["nodes"] = [{"name": "Triangle", "mesh": 0}]
    doc["scenes"] = [{"nodes": [0]}]
    doc["scene"] = 0
    write_gltf("occlusion-emissive.gltf", doc, buf)


def write_conflict():
    """One mesh with two primitives: one image is the first material's base colour and the second's normal map."""
    doc = {"asset": {"version": "2.0", "generator": "FenrirEngine make_fixtures.py"}}
    buf = Buffer(doc)
    position = buf.floats(MARKER_POSITIONS, "VEC3")
    uv = buf.floats([(0, 0), (1, 0), (0, 1)], "VEC2")
    index = buf.indices(MARKER_INDICES)
    prims = [{"attributes": {"POSITION": position, "TEXCOORD_0": uv}, "indices": index, "material": m} for m in (0, 1)]
    doc["meshes"] = [{"name": "Shared", "primitives": prims}]
    doc["images"] = [{"uri": "data:image/png;base64," + base64.b64encode(CHECKER).decode("ascii")}]
    doc["textures"] = [{"source": 0}]
    doc["materials"] = [{"name": "Colour", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}},
                        {"name": "Bumps", "normalTexture": {"index": 0}}]
    doc["nodes"] = [{"name": "Shared", "mesh": 0}]
    doc["scenes"] = [{"nodes": [0]}]
    doc["scene"] = 0
    write_gltf("conflict.gltf", doc, buf)


# Skinned FBX fixtures (ADR-0031's FBX list), ASCII FBX 7.4. Matrices are 16 floats, column-major as FBX and glm store
# them. A cluster's TransformLink is its bone's world at the bind (ufbx's bind_to_world) and its Transform the mesh
# node's world at the bind in that bone's space (geometry_to_bone, with no geometric transform).

KTIME_PER_SECOND = 46186158000
# KeyAttrFlags: linear keys, and cubic keys with user tangents (slopes in KeyAttrDataFloat).
KEY_LINEAR = 0x4
KEY_CUBIC_USER = 0x8 | 0x400


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def euler(rx, ry, rz):
    """FBX's Lcl Rotation in its default XYZ order (X first, then Y, then Z) as a quaternion (x, y, z, w)."""
    return qmul(quat((0, 0, 1), rz), qmul(quat((0, 1, 0), ry), quat((1, 0, 0), rx)))


class Fbx:
    """An FBX document being written: objects, connections, and each model's rest world."""

    def __init__(self, up_axis=1, unit_scale=100):
        self.objects = []
        self.connections = []
        self.parents = {}
        self.locals = {}
        self.next_uid = 1000
        # UpAxis 1 is +Y, 2 is +Z (with front -Y, as Blender and 3ds Max write); UnitScaleFactor is centimetres per
        # unit, so 100 is metres and 1 centimetres.
        if up_axis == 1:
            axes = (1, 1, 2, 1, 0, 1)
        else:
            axes = (2, 1, 1, -1, 0, 1)
        self.header = ("; FBX 7.4.0 project file\n"
                       "; FenrirAssets model fixture, written by hand-rolled make_fixtures.py (no FBX SDK involved).\n"
                       "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n"
                       '\tCreator: "FenrirEngine make_fixtures.py"\n}\n'
                       "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
                       '\t\tP: "UpAxis", "int", "Integer", "",%d\n\t\tP: "UpAxisSign", "int", "Integer", "",%d\n'
                       '\t\tP: "FrontAxis", "int", "Integer", "",%d\n\t\tP: "FrontAxisSign", "int", "Integer", "",%d\n'
                       '\t\tP: "CoordAxis", "int", "Integer", "",%d\n\t\tP: "CoordAxisSign", "int", "Integer", "",%d\n'
                       % axes +
                       '\t\tP: "UnitScaleFactor", "double", "Number", "",%g\n'
                       '\t\tP: "OriginalUnitScaleFactor", "double", "Number", "",%g\n\t}\n}\n'
                       % (unit_scale, unit_scale))

    def uid(self):
        self.next_uid += 1
        return self.next_uid

    def connect(self, child, parent, prop=None):
        if prop:
            self.connections.append('\tC: "OP",%d,%d, "%s"' % (child, parent, prop))
        else:
            self.connections.append('\tC: "OO",%d,%d' % (child, parent))

    def model(self, name, kind, parent=None, t=None, r=None, s=None):
        """A model; `kind` is Null, LimbNode or Mesh, `r` Euler degrees. A LimbNode gets its skeleton attribute."""
        uid = self.uid()
        self.objects.append(fbx_model(uid, name, kind, t, r, s))
        self.connect(uid, parent or 0)
        self.parents[uid] = parent
        self.locals[uid] = trs(t or (0, 0, 0), euler(*(r or (0, 0, 0))), s or (1, 1, 1))
        if kind == "LimbNode":
            attribute = self.uid()
            self.objects.append('\tNodeAttribute: %d, "NodeAttribute::%s", "LimbNode" {\n'
                                '\t\tTypeFlags: "Skeleton"\n\t}\n' % (attribute, name))
            self.connect(attribute, uid)
        return uid

    def world(self, uid):
        parent = self.parents[uid]
        return self.locals[uid] if parent is None else mul(self.world(parent), self.locals[uid])

    def mesh(self, name, parent, positions, triangles=None, t=None, **streams):
        """A mesh model with its geometry, triangles over `positions` (one triangle when none are given), and
        fbx_geometry's per-vertex `streams` (normals, uvs, tangents, binormals)."""
        model = self.model(name, "Mesh", parent, t=t)
        triangles = triangles or [(0, 1, 2)]
        polygon = [i for a, b, c in triangles for i in (a, b, -c - 1)]
        geometry = self.uid()
        self.objects.append(fbx_geometry(geometry, name, positions, polygon, **streams))
        self.connect(geometry, model)
        return model, geometry

    def skin(self, name, model, geometry, clusters):
        """A skin on a mesh: each cluster is (bone, {vertex: weight}, bind world of the bone)."""
        skin = self.uid()
        self.objects.append('\tDeformer: %d, "Deformer::%s", "Skin" {\n\t\tVersion: 101\n'
                            '\t\tLink_DeformAcuracy: 50\n\t}\n' % (skin, name))
        self.connect(skin, geometry)
        mesh_world = self.world(model)
        for bone, weights, bind in clusters:
            cluster = self.uid()
            body = '\tDeformer: %d, "SubDeformer::%s", "Cluster" {\n\t\tVersion: 100\n' % (cluster, name)
            if weights:
                body += fbx_array("Indexes", list(weights.keys()), "a")
                body += fbx_array("Weights", list(weights.values()), "a")
            body += fbx_array("Transform", mul(inverse(bind), mesh_world))
            body += fbx_array("TransformLink", bind)
            body += "\t}\n"
            self.objects.append(body)
            self.connect(cluster, skin)
            self.connect(bone, cluster)

    def clip(self, name, seconds, curves):
        """An animation stack of one layer: each curve is (model, property, axis, keys, flags), its keys (time in
        seconds, value) or (time, value, right slope, next left slope)."""
        stack = self.uid()
        self.objects.append('\tAnimationStack: %d, "AnimStack::%s", "" {\n\t\tProperties70:  {\n'
                            '\t\t\tP: "LocalStart", "KTime", "Time", "",0\n'
                            '\t\t\tP: "LocalStop", "KTime", "Time", "",%d\n\t\t}\n\t}\n'
                            % (stack, name, round(seconds * KTIME_PER_SECOND)))
        layer = self.uid()
        self.objects.append('\tAnimationLayer: %d, "AnimLayer::BaseLayer", "" {\n\t}\n' % layer)
        self.connect(layer, stack)
        nodes = {}
        for model, prop, axis, keys, flags in curves:
            if (model, prop) not in nodes:
                node = self.uid()
                short = {"Lcl Translation": "T", "Lcl Rotation": "R", "Lcl Scaling": "S"}[prop]
                self.objects.append('\tAnimationCurveNode: %d, "AnimCurveNode::%s", "" {\n\t\tProperties70:  {\n'
                                    '\t\t\tP: "d|X", "Number", "", "A",0\n\t\t\tP: "d|Y", "Number", "", "A",0\n'
                                    '\t\t\tP: "d|Z", "Number", "", "A",0\n\t\t}\n\t}\n' % (node, short))
                self.connect(node, layer)
                self.connect(node, model, prop)
                nodes[(model, prop)] = node
            curve = self.uid()
            slopes = [(k[2], k[3]) if len(k) > 2 else (0, 0) for k in keys]
            body = '\tAnimationCurve: %d, "AnimCurve::", "" {\n\t\tDefault: 0\n\t\tKeyVer: 4009\n' % curve
            body += fbx_array("KeyTime", [round(k[0] * KTIME_PER_SECOND) for k in keys], "a")
            body += fbx_array("KeyValueFloat", [k[1] for k in keys], "a")
            body += fbx_array("KeyAttrFlags", [flags] * len(keys), "a")
            body += fbx_array("KeyAttrDataFloat", [v for right, left in slopes for v in (right, left, 0, 0)], "a")
            body += fbx_array("KeyAttrRefCount", [1] * len(keys), "a")
            body += "\t}\n"
            self.objects.append(body)
            self.connect(curve, nodes[(model, prop)], "d|" + axis)

    def write(self, name):
        text = self.header + "Objects:  {\n" + "".join(self.objects) + "}\n"
        text += "Connections:  {\n" + "\n".join(self.connections) + "\n}\n"
        write(name, text)


def write_fbx_armatures():
    """The FBX half of armatures.gltf: unrelated armatures, the bone rules, meshes sharing a skeleton, duplicate names.

    Scene                Null
      Base               LimbNode, no joint: a marked bone above the joints, T(0, 0.5, 0)
        Hips             LimbNode, joint of BodyA's skin, T(0, 0.5, 0)
          Spine          LimbNode, no joint, between Hips and Chest: an intermediate bone, T(0, 0.5, 0)
            Chest        LimbNode, joint of both body skins, T(0, 0.5, 0)
              Head       LimbNode, a joint no vertex is weighted to: an unweighted leaf, T(0, 0.5, 0)
                HeadEnd  LimbNode, no joint: a marked leaf, T(0, 0.2, 0)
              Socket     Null, an empty child of a bone: an ordinary node (an attachment), T(0.2, 0, 0)
              Hat        Mesh child of a bone, animated: still ordinary, T(0, 0.6, 0)
              Tail       Null, an empty child of a bone, animated: a bone, T(0, 0, -0.3)
      Pelvis             LimbNode, joint of the Legs skin, T(3, 1, 0)
        Pelvis           LimbNode, joint, a repeated name, T(0, 0.5, 0)
      Rig                Null, T(-3, 0, 0)
        Root             LimbNode, joint of the Prop skin, T(0, 1, 0)
      BodyA              skinned to Hips, Chest and Head (BodyASkin); BodyB skinned to Chest alone (BodyBSkin): two
      BodyB              skins sharing a joint, in one armature, so one skeleton
      Legs, Prop         skinned to the Pelvis pair (LegsSkin) and to Root (PropSkin)
    Every mesh node sits at the origin. The rest pose is the bind pose. The clip "Wave" turns Spine and Tail about Z
    and lifts Hat, over 1 s with linear keys.
    """
    fbx = Fbx()
    scene = fbx.model("Scene", "Null")
    base = fbx.model("Base", "LimbNode", scene, t=(0, 0.5, 0))
    hips = fbx.model("Hips", "LimbNode", base, t=(0, 0.5, 0))
    spine = fbx.model("Spine", "LimbNode", hips, t=(0, 0.5, 0))
    chest = fbx.model("Chest", "LimbNode", spine, t=(0, 0.5, 0))
    head = fbx.model("Head", "LimbNode", chest, t=(0, 0.5, 0))
    fbx.model("HeadEnd", "LimbNode", head, t=(0, 0.2, 0))
    fbx.model("Socket", "Null", chest, t=(0.2, 0, 0))
    hat, _ = fbx.mesh("Hat", chest, MARKER_POSITIONS, t=(0, 0.6, 0))
    tail = fbx.model("Tail", "Null", chest, t=(0, 0, -0.3))
    pelvis = fbx.model("Pelvis", "LimbNode", scene, t=(3, 1, 0))
    pelvis2 = fbx.model("Pelvis", "LimbNode", pelvis, t=(0, 0.5, 0))
    holder = fbx.model("Rig", "Null", scene, t=(-3, 0, 0))
    root = fbx.model("Root", "LimbNode", holder, t=(0, 1, 0))

    body_a, geometry = fbx.mesh("BodyA", scene, [(0, 1, 0), (0.5, 1.5, 0), (0, 2, 0)])
    fbx.skin("BodyASkin", body_a, geometry, [(hips, {0: 1, 1: 0.5}, fbx.world(hips)),
                                             (chest, {1: 0.5, 2: 1}, fbx.world(chest)),
                                             (head, {}, fbx.world(head))])
    body_b, geometry = fbx.mesh("BodyB", scene, [(0, 2, 0), (0.5, 2, 0), (0, 2.5, 0)])
    fbx.skin("BodyBSkin", body_b, geometry, [(chest, {0: 1, 1: 1, 2: 1}, fbx.world(chest))])
    legs, geometry = fbx.mesh("Legs", scene, [(3, 1, 0), (3.5, 1, 0), (3, 1.5, 0)])
    fbx.skin("LegsSkin", legs, geometry, [(pelvis, {0: 0.25, 1: 1, 2: 1}, fbx.world(pelvis)),
                                          (pelvis2, {0: 0.75}, fbx.world(pelvis2))])
    prop, geometry = fbx.mesh("Prop", scene, [(-3, 1, 0), (-2, 1, 0), (-3, 2, 0)])
    fbx.skin("PropSkin", prop, geometry, [(root, {0: 1, 1: 1, 2: 1}, fbx.world(root))])

    fbx.clip("Wave", 1, [(spine, "Lcl Rotation", "Z", [(0, 0), (1, 30)], KEY_LINEAR),
                         (tail, "Lcl Rotation", "Z", [(0, 0), (1, 45)], KEY_LINEAR),
                         (hat, "Lcl Translation", "Y", [(0, 0.6), (1, 0.8)], KEY_LINEAR)])
    fbx.write("armatures.fbx")


def write_fbx_shared_skins():
    """The FBX half of shared-skins.gltf: Rig > Hip > Knee > Foot (LimbNodes, T(0, 1, 0), (0, -0.5, 0), (0, -0.5, 0)).

    ThighSkin [Hip, Knee] and ShinSkin [Knee, Foot] bind at the rest, so they merge. BootSkin [Foot, Knee] binds at
    C^-1 x the rest worlds, C = T(0, 0, 2): its inverse binds are the others' x C, its vertices are written at their
    bind positions minus (0, 0, 2), and one conversion takes them into the others'.
    """
    fbx = Fbx()
    holder = fbx.model("Rig", "Null")
    hip = fbx.model("Hip", "LimbNode", holder, t=(0, 1, 0))
    knee = fbx.model("Knee", "LimbNode", hip, t=(0, -0.5, 0))
    foot = fbx.model("Foot", "LimbNode", knee, t=(0, -0.5, 0))
    undo = trs(t=(0, 0, -2))
    thigh, geometry = fbx.mesh("Thigh", holder, [(0, 1, 0), (0.2, 0.75, 0), (0, 0.5, 0)])
    fbx.skin("ThighSkin", thigh, geometry, [(hip, {0: 1, 1: 0.5}, fbx.world(hip)),
                                            (knee, {1: 0.5, 2: 1}, fbx.world(knee))])
    shin, geometry = fbx.mesh("Shin", holder, [(0, 0.5, 0), (0.2, 0.25, 0), (0, 0, 0)])
    fbx.skin("ShinSkin", shin, geometry, [(knee, {0: 1, 1: 0.5}, fbx.world(knee)),
                                          (foot, {1: 0.5, 2: 1}, fbx.world(foot))])
    boot, geometry = fbx.mesh("Boot", holder, [(0, 0, -2), (0.3, 0, -2), (0, 0.2, -2)])
    fbx.skin("BootSkin", boot, geometry, [(foot, {0: 1, 1: 1, 2: 0.5}, mul(undo, fbx.world(foot))),
                                          (knee, {2: 0.5}, mul(undo, fbx.world(knee)))])
    fbx.write("shared-skins.fbx")


def write_fbx_incompatible_skin():
    """The FBX half of incompatible-skin.gltf: Rig (with the rigid mesh Badge) > Arm > Hand, LimbNodes.

    SleeveSkin [Arm, Hand] binds at the rest. GloveSkin [Hand, Arm] binds Hand at T(-1, 0, 0) and Arm at T(0, -1, 0)
    from their rest worlds: no one conversion takes its binds into SleeveSkin's, so it fails. Other > Bone (a
    LimbNode), with CloakSkin naming Bone in two clusters: its skeleton fails, and Bone stays an ordinary node.
    """
    fbx = Fbx()
    holder = fbx.model("Rig", "Mesh")
    geometry = fbx.uid()
    fbx.objects.append(fbx_geometry(geometry, "Badge", MARKER_POSITIONS, [0, 1, -3]))
    fbx.connect(geometry, holder)
    arm = fbx.model("Arm", "LimbNode", holder, t=(0, 1, 0))
    hand = fbx.model("Hand", "LimbNode", arm, t=(1, 0, 0))
    other = fbx.model("Other", "Null", t=(0, 0, 5))
    bone = fbx.model("Bone", "LimbNode", other, t=(0, 1, 0))
    triangle = [(0, 1, 0), (1, 1, 0), (0, 2, 0)]
    sleeve, geometry = fbx.mesh("Sleeve", holder, triangle)
    fbx.skin("SleeveSkin", sleeve, geometry, [(arm, {0: 1, 1: 1, 2: 1}, fbx.world(arm)),
                                              (hand, {}, fbx.world(hand))])
    glove, geometry = fbx.mesh("Glove", holder, triangle)
    fbx.skin("GloveSkin", glove, geometry, [(hand, {0: 1, 1: 1, 2: 1}, mul(trs(t=(-1, 0, 0)), fbx.world(hand))),
                                            (arm, {}, mul(trs(t=(0, -1, 0)), fbx.world(arm)))])
    cloak, geometry = fbx.mesh("Cloak", other, triangle)
    fbx.skin("CloakSkin", cloak, geometry, [(bone, {0: 1, 1: 1, 2: 1}, fbx.world(bone)),
                                            (bone, {}, fbx.world(bone))])
    fbx.write("incompatible-skin.fbx")


# units.fbx's numbers, in its own space (Z up, centimetres), which the tests repeat.
UNITS_OWNER_T, UNITS_OWNER_R, UNITS_OWNER_S = (100, 200, 300), (30, 0, 45), (2, 0.5, 1.5)
UNITS_UPPER_T, UNITS_UPPER_R = (0, 0, 100), (0, 20, 0)
UNITS_LOWER_T, UNITS_LOWER_R = (0, 0, 100), (-35, 0, 0)
UNITS_SLEEVE = [(10, 0, 100), (0, 20, 150), (-10, 0, 200), (20, -10, 250)]
UNITS_SLEEVE_WEIGHTS = [(0.7, 0.3), (0.5, 0.5), (0.4, 0.6), (0, 1)]


def write_fbx_units():
    """A scaled-unit, rotated-axis file: Z up and centimetres, which ufbx converts to +Y up and metres on its root.

    Owner      Null at T(100, 200, 300) cm, R (30, 0, 45) degrees, S(2, 0.5, 1.5)
      Upper    LimbNode, T(0, 0, 100) cm, R (0, 20, 0) degrees
        Lower  LimbNode, T(0, 0, 100) cm, R (-35, 0, 0) degrees
      Sleeve   a mesh skinned to Upper and Lower with two influences per vertex, at Owner's origin
    The rest pose is the bind pose. The vertices are written in Sleeve's space, in centimetres.
    """
    fbx = Fbx(up_axis=2, unit_scale=1)
    owner = fbx.model("Owner", "Null", t=UNITS_OWNER_T, r=UNITS_OWNER_R, s=UNITS_OWNER_S)
    upper = fbx.model("Upper", "LimbNode", owner, t=UNITS_UPPER_T, r=UNITS_UPPER_R)
    lower = fbx.model("Lower", "LimbNode", upper, t=UNITS_LOWER_T, r=UNITS_LOWER_R)
    sleeve, geometry = fbx.mesh("Sleeve", owner, UNITS_SLEEVE, [(0, 1, 2), (1, 3, 2)])
    fbx.skin("SleeveSkin", sleeve, geometry,
             [(upper, {v: w[0] for v, w in enumerate(UNITS_SLEEVE_WEIGHTS) if w[0]}, fbx.world(upper)),
              (lower, {v: w[1] for v, w in enumerate(UNITS_SLEEVE_WEIGHTS) if w[1]}, fbx.world(lower))])
    fbx.write("units.fbx")


def write_fbx_baked_clip():
    """A clip for ufbx to bake: Rig > Joint (LimbNode, T(0, 1, 0)), with Body skinned to it, and the clip "Lift".

    Lift runs 1 s. Joint's Y translation is a cubic curve from 1 to 2 with flat user tangents, 1 + 3t^2 - 2t^3; its Z
    rotation is linear from 0 to 270 degrees, which FBX interpolates as an Euler angle, so ufbx resamples it, and the
    quaternions cross a hemisphere on the way.
    """
    fbx = Fbx()
    holder = fbx.model("Rig", "Null")
    joint = fbx.model("Joint", "LimbNode", holder, t=(0, 1, 0))
    body, geometry = fbx.mesh("Body", holder, [(0, 1, 0), (1, 1, 0), (0, 2, 0)])
    fbx.skin("BodySkin", body, geometry, [(joint, {0: 1, 1: 1, 2: 1}, fbx.world(joint))])
    fbx.clip("Lift", 1, [(joint, "Lcl Translation", "Y", [(0, 1, 0, 0), (1, 2, 0, 0)], KEY_CUBIC_USER),
                         (joint, "Lcl Rotation", "Z", [(0, 0), (1, 270)], KEY_LINEAR)])
    fbx.write("baked-clip.fbx")


def write_fbx_clips_only():
    """A file of clips, as animation packs ship: Armature (Null) > Root > Arm (LimbNodes, T(0, 1, 0) each), and a
    second tree beside Root, Armature > Control (a LimbNode, T(1, 0, 0), as a rig's controls are), with no mesh.

    The clip "Raise" turns Arm about X from 0 to 90 degrees over 1 s with linear keys.
    """
    fbx = Fbx()
    armature = fbx.model("Armature", "Null")
    root = fbx.model("Root", "LimbNode", armature, t=(0, 1, 0))
    arm = fbx.model("Arm", "LimbNode", root, t=(0, 1, 0))
    fbx.model("Control", "LimbNode", armature, t=(1, 0, 0))
    fbx.clip("Raise", 1, [(arm, "Lcl Rotation", "X", [(0, 0), (1, 90)], KEY_LINEAR)])
    fbx.write("clips-only.fbx")


# The tangents fixtures' quads: the panel's, U mirrored for "Mirrored" (whose tangent then runs along -X).
MIRRORED_UVS = [(1 - u, v) for u, v in PANEL_UVS]
PANEL_TRIANGLES = [(0, 1, 2), (0, 2, 3)]


def write_tangents():
    """Three quads in the XY plane facing +Z, as the scene's Panel, side by side, for the tangent stream (ADR-0048).

    Plain     T(-3, 0, 0), the panel's UVs, tangent (1, 0, 0) and handedness +1: the bitangent N x T is +Y, up the image
    Mirrored  T(0, 0, 0), U mirrored, tangent (-1, 0, 0) and handedness -1: the bitangent is still +Y
    Bare      T(3, 0, 0), normals and UVs but no tangents
    tangents.gltf writes glTF's TANGENT (xyz, the handedness in w); tangents.fbx writes FBX's tangent and binormal
    layers, the binormal +Y for both, so the importer finds each handedness from them.
    """
    doc, buf = new_document()
    rig = Rig(doc)
    quads = (("Plain", -3, PANEL_UVS, (1, 0, 0, 1)), ("Mirrored", 0, MIRRORED_UVS, (-1, 0, 0, -1)),
             ("Bare", 3, PANEL_UVS, None))
    for name, x, uvs, tangent in quads:
        attributes = {"POSITION": buf.floats(PANEL_POSITIONS, "VEC3"), "NORMAL": buf.floats(PANEL_NORMALS, "VEC3"),
                      "TEXCOORD_0": buf.floats(uvs, "VEC2")}
        if tangent:
            attributes["TANGENT"] = buf.floats([tangent] * 4, "VEC4")
        doc.setdefault("meshes", []).append(
            {"name": name, "primitives": [{"attributes": attributes, "indices": buf.indices(PANEL_INDICES)}]})
        rig.node(name, t=(x, 0, 0), mesh=len(doc["meshes"]) - 1)
    rig.finish()
    doc["scenes"] = [{"nodes": list(range(len(doc["nodes"])))}]
    doc["scene"] = 0
    write_gltf("tangents.gltf", doc, buf)

    fbx = Fbx()
    for name, x, uvs, tangent in quads:
        streams = {"normals": PANEL_NORMALS, "uvs": uvs}
        if tangent:
            streams["tangents"] = [tangent[:3]] * 4
            streams["binormals"] = [(0, 1, 0)] * 4
        fbx.mesh(name, None, PANEL_POSITIONS, PANEL_TRIANGLES, t=(x, 0, 0), **streams)
    fbx.write("tangents.fbx")


def main():
    write("checker.png", CHECKER)
    write("normal.png", NORMAL)
    doc, buf = scene_document(embed_images=False)
    write_gltf("scene.gltf", doc, buf)
    doc, buf = scene_document(embed_images=True)
    write_glb("scene.glb", doc, buf)
    write_obj()
    write_fbx()
    write_skinned()
    write_armatures()
    write_shared_skins()
    write_incompatible_skin()
    write_owners()
    write_cubic_clip()
    write_fbx_armatures()
    write_fbx_shared_skins()
    write_fbx_incompatible_skin()
    write_fbx_units()
    write_fbx_baked_clip()
    write_fbx_clips_only()
    write_missing_texture()
    write_occlusion_emissive()
    write_conflict()
    write_tangents()
    # Not glTF at all: the importer's failure.
    write("corrupt.gltf", "{ this is not JSON\n")


if __name__ == "__main__":
    main()
