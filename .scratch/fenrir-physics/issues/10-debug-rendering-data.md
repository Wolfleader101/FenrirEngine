# What does debug rendering data look like?

Type: grilling
Status: open
Blocked by: 01, 03

## Question

Physics produces debug rendering data as plain data that any renderer may draw, with neither package depending on the other. What is it?

- Its shape: lines, triangles, wire shapes, text? Colours, per-category toggles (shapes, contacts, body velocities, broad-phase bounds, sleeping state)?
- Generated through Jolt's `DebugRenderer`, or by FenrirPhysics from its own body and shape data (which would survive a backend swap)?
- When it is produced (after each step, on demand), where it lives, and how a renderer reads it.
- Its cost when switched off, and whether it is compiled out of release builds.
- How it supports testing (e.g. asserting on produced shapes) as well as viewing.
