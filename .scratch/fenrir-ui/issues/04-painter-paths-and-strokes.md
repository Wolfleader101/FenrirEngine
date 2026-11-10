# 04: Painter paths and strokes

**What to build:** arbitrary paths fill and stroke with anti-aliased edges through the same painter and vertex (spec: "FenrirPainter", technique; ADR-0060's gap 2).

- **Paths**: move, line, quadratic, cubic, arc, arc-to, close, with rectangle, rounded rectangle, circle and ellipse helpers; non-zero or even-odd fill rule; a path is a value the caller may keep and transform.
- **Fills**: flattened to a tolerance in pixels under the current transform, split into simple polygons by winding with holes bridged, ear-clipped into triangles, with a one-pixel fringe ring emitted as shape kind "triangle" so the shader applies only paint and clip.
- **Strokes**: width, cap (butt, round, square), join (miter with limit, round, bevel), dash array and offset; hairlines as one pixel; offset outlines joined and capped, with fringes; `Fill` of a stroked path's outline is the same geometry.
- Polygons and polylines as helpers over paths; a stroked circle or rounded rectangle stays analytic (ticket 03's ring and border) when no dash is set.

**Blocked by:** 03

**Touches:** `packages/FenrirPainter/`

**Status:** ready-for-agent

- [ ] Draw-list tests: a filled square path is two triangles plus its fringe; a concave path with a hole has the expected triangle count within a tolerance and its fringe follows the outline; even-odd versus non-zero on a self-intersecting path; a dashed stroke emits the expected number of segments; each join and cap kind emits the expected geometry; flattening tolerance halves the segment count when doubled.
- [ ] GL test (skipped without a display): a stroked star and a filled ring-with-hole read back with anti-aliased edges and no double coverage at joins.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
