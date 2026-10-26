"""Counts, per facade, the public members that are exact pass-throughs of a generated <Package>Native declaration.

A member is an exact pass-through when it is an expression-bodied `=> XNative.F(a, b, ...)` whose parameters, by
modifier and type in order, equal F's declaration, every argument is its parameter with the same modifier, no parameter
has a default, and the result type equals F's. The other buckets say what stops the rest.
"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(sys.argv[1])

NATIVE_DECL = re.compile(r"public static partial (?P<ret>[\w<>\[\]?]+) (?P<name>\w+)\((?P<params>[^)]*)\);", re.S)
MEMBER = re.compile(
    r"\n    public (?:const |static )?(?P<ret>[\w<>\[\]?.]+) (?P<name>\w+)(?P<generic><\w+>)?"
    r"(?:\((?P<params>[^)]*)\))?(?P<where> where [^=\n{]+)?\s*(?P<body>=>[^;]*;|\{.*?\n    \}|=[^;]*;)",
    re.S,
)
CALL = re.compile(r"=>\s*(?P<cls>\w+Native)\.(?P<fn>\w+)\((?P<args>.*)\);\s*$", re.S)


def split_params(text):
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return [" ".join(p.split()) for p in out]


def param_shape(p):
    """(modifier, type, name, default)"""
    default = None
    if "=" in p:
        p, default = [s.strip() for s in p.split("=", 1)]
    parts = p.split()
    mod = ""
    if parts[0] in ("in", "out", "ref"):
        mod = parts.pop(0)
    return mod, parts[0], parts[1], default


def arg_shape(a):
    parts = a.split()
    mod = ""
    if parts[0] in ("in", "out", "ref"):
        mod = parts.pop(0)
    return mod, " ".join(parts)


def load_natives(path):
    natives = {}
    for m in NATIVE_DECL.finditer(path.read_text(encoding="utf-8")):
        natives[m["name"]] = (m["ret"], [param_shape(p) for p in split_params(m["params"])])
    return natives


FACADES = {
    "Window.cs": "WindowNative.cs",
    "Devices.cs": "InputNative.cs",
    "PhysicsWorld.cs": "PhysicsNative.cs",
    "DebugDraw.cs": "DebugDrawNative.cs",
    "Renderer.cs": "RendererNative.cs",
    "Materials.cs": "AssetsNative.cs",
    "Assets.cs": "AssetsNative.cs",
    "Shaders.cs": "AssetsNative.cs",
}

totals = Counter()
used_natives = defaultdict(set)
for facade, native_file in FACADES.items():
    natives = load_natives(ROOT / native_file)
    text = (ROOT / facade).read_text(encoding="utf-8")
    # Only the static classes' bodies: skip partial structs by starting at the first `public static class`.
    text = text[text.index("public static class"):]
    buckets = Counter()
    detail = defaultdict(list)
    for m in MEMBER.finditer(text):
        name, body = m["name"], m["body"]
        if m["params"] is None and body.startswith("="):
            continue  # a const
        if m["params"] is None:
            buckets["property"] += 1
            detail["property"].append(name)
            continue
        call = CALL.match(body.strip()) if body.strip().startswith("=>") else None
        if not call:
            buckets["body/conversion"] += 1
            detail["body/conversion"].append(name)
            continue
        fn = call["fn"]
        used_natives[native_file].add(fn)
        if fn not in natives:
            buckets["body/conversion"] += 1
            detail["body/conversion"].append(name)
            continue
        ret, nparams = natives[fn]
        fparams = [param_shape(p) for p in split_params(m["params"])]
        args = [arg_shape(a) for a in split_params(call["args"])]
        has_default = any(p[3] is not None for p in fparams)
        plain_args = all(a[1] == p[2] for a, p in zip(args, fparams)) and len(args) == len(fparams)
        reordered = not plain_args and sorted(a[1] for a in args) == sorted(p[2] for p in fparams) and len(args) == len(fparams)
        same_types = [p[1] for p in fparams] == [p[1] for p in nparams] if plain_args else None
        same_mods = plain_args and all(a[0] == p[0] == n[0] for a, p, n in zip(args, fparams, nparams))
        in_added = plain_args and all((a[0] == n[0]) and (p[0] == a[0] or (p[0] == "" and a[0] == "in")) for a, p, n in zip(args, fparams, nparams))
        if m["generic"]:
            b = "generic"
        elif plain_args and same_types and same_mods and not has_default and ret == m["ret"]:
            b = "exact"
        elif plain_args and same_types and in_added and not has_default and ret == m["ret"]:
            b = "value-to-in"
        elif has_default and (plain_args or reordered) and (same_types or reordered):
            b = "defaults" + ("+reorder" if reordered else "")
        elif reordered:
            b = "reorder"
        else:
            b = "body/conversion"
        buckets[b] += 1
        detail[b].append(f"{name}->{fn}" if name != fn else name)
    total = sum(buckets.values())
    print(f"\n{facade} ({total} public members over {native_file}, {len(natives)} natives)")
    for b, n in sorted(buckets.items(), key=lambda kv: -kv[1]):
        print(f"  {b:16} {n:3}  {', '.join(detail[b])}")
    totals.update(buckets)
    totals["members"] += total

print("\nTOTAL")
for b, n in sorted(totals.items(), key=lambda kv: -kv[1]):
    print(f"  {b:16} {n:3}")
for native_file, used in used_natives.items():
    natives = load_natives(ROOT / native_file)
    unused = sorted(set(natives) - used)
    print(f"{native_file}: {len(natives)} natives, {len(unused)} not called by an expression-bodied facade member: {unused}")
