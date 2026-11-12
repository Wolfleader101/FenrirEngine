"""Converts Yoga's generated conformance fixtures into element trees with inline styles, for FenrirUI's layout tests.

Usage, from the repository root:

    python packages/FenrirUI/Tests/fixtures/convert-yoga-fixtures.py

Reads packages/FenrirUI/libs/yoga/tests/generated/*.cpp (gentest's output: each test builds a tree of nodes with style
setters, lays it out left to right with no available size, and asserts every node's rectangle) and writes
packages/FenrirUI/Tests/fixtures/YogaFixtures.inc: one entry per test, each node its parent's index, its style as CSS
longhands ("name: value; ..."), and the rectangle Yoga's left-to-right layout asserts, relative to its parent.

A test is left out when it uses what FenrirUI's property set does not have (start and end edges, box-sizing, display
contents, static positioning, overflow scroll, an auto inset, measure functions) or gtest skips it. The build runs no
Python: the output is checked in, and run again when the Yoga submodule moves.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
GENERATED = ROOT / "packages/FenrirUI/libs/yoga/tests/generated"
OUTPUT = pathlib.Path(__file__).resolve().parent / "YogaFixtures.inc"

EDGES = {"YGEdgeLeft": "left", "YGEdgeTop": "top", "YGEdgeRight": "right", "YGEdgeBottom": "bottom"}
GUTTERS = {"YGGutterColumn": "column-gap", "YGGutterRow": "row-gap"}

ENUMS = {
    "YGFlexDirectionColumn": "column",
    "YGFlexDirectionColumnReverse": "column-reverse",
    "YGFlexDirectionRow": "row",
    "YGFlexDirectionRowReverse": "row-reverse",
    "YGJustifyFlexStart": "flex-start",
    "YGJustifyCenter": "center",
    "YGJustifyFlexEnd": "flex-end",
    "YGJustifySpaceBetween": "space-between",
    "YGJustifySpaceAround": "space-around",
    "YGJustifySpaceEvenly": "space-evenly",
    "YGAlignAuto": "auto",
    "YGAlignFlexStart": "flex-start",
    "YGAlignCenter": "center",
    "YGAlignFlexEnd": "flex-end",
    "YGAlignStretch": "stretch",
    "YGAlignBaseline": "baseline",
    "YGAlignSpaceBetween": "space-between",
    "YGAlignSpaceAround": "space-around",
    "YGAlignSpaceEvenly": "space-evenly",
    "YGPositionTypeRelative": "relative",
    "YGPositionTypeAbsolute": "absolute",
    "YGWrapNoWrap": "nowrap",
    "YGWrapWrap": "wrap",
    "YGWrapWrapReverse": "wrap-reverse",
    "YGOverflowVisible": "visible",
    "YGOverflowHidden": "hidden",
    "YGDisplayFlex": "flex",
    "YGDisplayNone": "none",
}

ENUM_SETTERS = {
    "FlexDirection": "flex-direction",
    "JustifyContent": "justify-content",
    "AlignItems": "align-items",
    "AlignSelf": "align-self",
    "AlignContent": "align-content",
    "PositionType": "position",
    "FlexWrap": "flex-wrap",
    "Overflow": "overflow",
    "Display": "display",
}

SIZES = {
    "Width": "width",
    "Height": "height",
    "MinWidth": "min-width",
    "MinHeight": "min-height",
    "MaxWidth": "max-width",
    "MaxHeight": "max-height",
    "FlexBasis": "flex-basis",
}


class Unsupported(Exception):
    pass


def number(text):
    if "/" in text:
        # An aspect ratio written as a fraction.
        numerator, denominator = (float(part.strip().rstrip("f")) for part in text.split("/"))
        if denominator == 0:
            raise Unsupported(text)
        value = numerator / denominator
    else:
        value = float(text.rstrip("f"))
    return f"{value:g}"


def length(text, percent):
    return number(text) + ("%" if percent else "px")


def declaration(setter, args):
    """The CSS longhand for one YGNodeStyleSet call, as (name, value), or a list of them for a shorthand's setter."""
    if setter in ENUM_SETTERS:
        if args[1] not in ENUMS:
            raise Unsupported(args[1])
        return ENUM_SETTERS[setter], ENUMS[args[1]]
    for suffix, percent in (("Percent", True), ("", False)):
        base = setter[: len(setter) - len(suffix)] if suffix else setter
        if suffix and not setter.endswith(suffix):
            continue
        if base in SIZES:
            return SIZES[base], length(args[1], percent)
        if base in ("Margin", "Padding", "Position"):
            if args[1] not in EDGES:
                raise Unsupported(args[1])
            side = EDGES[args[1]]
            name = side if base == "Position" else f"{base.lower()}-{side}"
            return name, length(args[2], percent)
        if base == "Gap":
            if args[1] == "YGGutterAll":
                # `gap` is a shorthand, which inline styles do not take: its two longhands.
                value = length(args[2], percent)
                return [("row-gap", value), ("column-gap", value)]
            return GUTTERS[args[1]], length(args[2], percent)
    if setter in ("WidthAuto", "HeightAuto", "FlexBasisAuto"):
        return SIZES[setter[:-4]], "auto"
    if setter == "MarginAuto":
        if args[1] not in EDGES:
            raise Unsupported(args[1])
        return f"margin-{EDGES[args[1]]}", "auto"
    if setter == "Border":
        if args[1] not in EDGES:
            raise Unsupported(args[1])
        return f"border-{EDGES[args[1]]}-width", number(args[2]) + "px"
    if setter in ("FlexGrow", "FlexShrink", "AspectRatio"):
        return {"FlexGrow": "flex-grow", "FlexShrink": "flex-shrink", "AspectRatio": "aspect-ratio"}[setter], number(
            args[1]
        )
    raise Unsupported(setter)


CALL = re.compile(r"^\s*(YG\w+)\((.*)\);\s*$")
ASSERT = re.compile(r"^\s*ASSERT_FLOAT_EQ\(([-\d.]+f?), YGNodeLayoutGet(\w+)\((\w+)\)\);\s*$")
NEW_NODE = re.compile(r"^\s*YGNodeRef (\w+) = YGNodeNewWithConfig\(config\);\s*$")


def convert(name, body):
    """One test's nodes as (parent index, declarations, rectangle), or None when it is left out."""
    if "GTEST_SKIP" in body:
        return None
    nodes = {}
    order = []
    parents = {}
    styles = {}
    rects = {}
    layouts = 0
    for line in body.splitlines():
        created = NEW_NODE.match(line)
        if created:
            nodes[created.group(1)] = len(order)
            order.append(created.group(1))
            styles[created.group(1)] = []
            continue
        asserted = ASSERT.match(line)
        if asserted:
            if layouts == 1:
                rects.setdefault(asserted.group(3), {})[asserted.group(2)] = number(asserted.group(1))
            continue
        call = CALL.match(line)
        if not call:
            continue
        function, args = call.group(1), [a.strip() for a in call.group(2).split(",")]
        if function.startswith("YGNodeStyleSet"):
            try:
                found = declaration(function[len("YGNodeStyleSet") :], args)
                styles[args[0]].extend(found if isinstance(found, list) else [found])
            except Unsupported:
                return None
        elif function == "YGNodeInsertChild":
            parents[args[1]] = args[0]
        elif function == "YGNodeCalculateLayout":
            layouts += 1
            if layouts > 1:
                break
        elif function in ("YGConfigNew", "YGConfigFree", "YGNodeFreeRecursive"):
            pass
        else:
            return None
    if not order or ("position", "absolute") not in styles[order[0]]:
        return None
    result = []
    for node in order:
        parent = nodes[parents[node]] if node in parents else -1
        rect = rects[node]
        result.append((parent, styles[node], (rect["Left"], rect["Top"], rect["Width"], rect["Height"])))
    return result


def main():
    tests = re.compile(r"^TEST\(YogaTest, (\w+)\) \{\n(.*?)^\}", re.M | re.S)
    lines = [
        "// Generated by convert-yoga-fixtures.py from Yoga's tests/generated (the submodule's pinned release): do not",
        "// edit. Each entry: a fixture's name, then its nodes, each its parent's index (-1 for the fixture's root), its",
        "// inline style, and its laid-out rectangle relative to its parent (left, top, width, height).",
        "// clang-format off",
    ]
    kept = 0
    total = 0
    for path in sorted(GENERATED.glob("*.cpp")):
        for match in tests.finditer(path.read_text(encoding="utf-8")):
            total += 1
            nodes = convert(match.group(1), match.group(2))
            if nodes is None:
                continue
            kept += 1
            lines.append(f'{{"{path.stem}/{match.group(1)}", {{')
            for parent, style, rect in nodes:
                css = "; ".join(f"{n}: {v}" for n, v in style)
                lines.append(f'    {{{parent}, "{css}", {{{", ".join(f"{float(r)!r}f" for r in rect)}}}}},')
            lines.append("}},")
    lines.append("// clang-format on")
    OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{kept} of {total} fixtures converted into {OUTPUT}", file=sys.stderr)


if __name__ == "__main__":
    main()
