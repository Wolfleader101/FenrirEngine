"""Writes FenrirAssets's model fixtures: one small scene as glTF, GLB, FBX (ASCII 7.4) and OBJ, plus the skinned,
missing-texture, colour-space-conflict and corrupt glTF files. Run by hand (`python make_fixtures.py`) from any directory; the
files are written beside this script and checked in. The build never runs it.

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

    def floats(self, rows, kind):
        flat = [c for row in rows for c in row]
        view = self.view(struct.pack("<%df" % len(flat), *flat), 34962)
        accessor = {"bufferView": view, "componentType": 5126, "count": len(rows), "type": kind}
        if kind == "VEC3":
            accessor["min"] = [min(r[i] for r in rows) for i in range(3)]
            accessor["max"] = [max(r[i] for r in rows) for i in range(3)]
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
    text = ",".join("%g" % v for v in values)
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


def fbx_geometry(uid, name, positions, polygon, normals=None, uvs=None):
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
    write_missing_texture()
    write_conflict()
    # Not glTF at all: the importer's failure.
    write("corrupt.gltf", "{ this is not JSON\n")


if __name__ == "__main__":
    main()
