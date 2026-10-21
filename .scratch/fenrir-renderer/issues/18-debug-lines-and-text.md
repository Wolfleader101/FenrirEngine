# How are debug lines and text drawn?

Type: grilling
Status: claimed
Blocked by: 13

## Question

How does game code and another package's plain data get debug lines and text on screen?

- Lines (and points?) per frame, over the scene or depth-tested; from a span of plain data such as FenrirPhysics's `DebugLine`s, without the caller owning GPU buffers; who glues physics's span to the renderer (app code, a demo helper).
- Text in screen space and at world positions; font, size and colour; one built-in font or loaded fonts.
- Immediate mode (submitted each frame) or retained; from C++ and C#.
- In every build or debug builds only; cost when unused.
