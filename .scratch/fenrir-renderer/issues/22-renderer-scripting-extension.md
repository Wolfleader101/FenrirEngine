# What does the renderer's scripting extension give C#?

Type: grilling
Status: open
Blocked by: 14, 16, 17, 18

## Question

What does C# get from FenrirRenderer (and FenrirWindow and input, if they are scriptable), through scripting extensions as ADR-0009 describes?

- Which components are writable from C#, and how assets (meshes, materials, textures) are named from C#.
- Native functions for screen-to-world rays and window size (debug lines and text are FenrirDebugDraw's own extension, ADR-0025).
- Input from C# (if input's own ticket does not settle it).
- No-op rules when no renderer is installed, matching the physics extension's.
- Contract and library names, built only when scripting is on.
