# 03: Scene depth, normals and a colour snapshot for a pass

Spec: [Game passes](../spec.md). ADR-0087.

**What to build:** A game pass asks for inputs and gets them bound: scene depth and scene normals from the depth prepass, resolved to single-sample targets once per point under MSAA when any pass there asks, and a snapshot of scene colour at the pass's point, copied (and resolved under MSAA) only when asked. Nothing is copied or resolved that no pass asks for. A pass asking for an input the frame cannot give logs an error and is skipped.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`

**Status:** ready-for-agent

Stacked on 02.

- [ ] Null-backend tests: a pass asking for depth, normals or colour gets them bound; with MSAA on, depth and normals are resolved once per point however many passes read them.
- [ ] With no pass asking, no snapshot or resolve target is made.
- [ ] An impossible input logs an error and the pass is skipped.
