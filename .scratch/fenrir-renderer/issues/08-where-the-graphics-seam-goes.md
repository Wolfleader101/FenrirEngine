# Where does the graphics seam go, and is the backend picked at compile time or at runtime?

Type: grilling
Status: resolved
Blocked by: 01, 02

## Question

Where does the line between FenrirRenderer and the graphics API sit, and how is the backend chosen? **ADR.**

- Is the seam a thin interface over GPU resources and commands (buffers, textures, pipelines, passes, command recording), a higher-level renderer with one implementation per API, or something between?
- Is the backend picked at compile time (as ADR-0010 did for physics: one private backend, no virtual calls) or at runtime (an interface, backends chosen at start-up)? What would make us revisit it?
- Is the seam public (an engine-level graphics layer game code or other packages may use, e.g. a UI library) or private to FenrirRenderer?
- How are GPU objects named across the seam (handles, lifetimes, deferred destruction)?
- What is refused (native escape hatches, backend tuning) and what is accepted (behaviour differences)?

Consult `codebase-design` for the seam.

## Answer

ADR: `docs/adr/0016-fenrirrenderer-picks-its-graphics-backend-at-runtime-behind-a-private-graphics-device.md`. Glossary: Graphics API, Graphics backend, Graphics device.

- **Altitude:** two layers. The public FenrirRenderer (components, meshes, materials, cameras) over a private, thin graphics device shaped like the explicit APIs (buffers, textures, samplers, pipelines, binding groups, passes); OpenGL 4.5 (DSA, immutable storage, clip control, samplers, separate programs) emulates it; barriers stay in the backend.
- **Selection:** at runtime, through an abstract device. `RendererConfig` names `OpenGL` (default) or `Null`; the null backend is always built, needs no window or context, and serves tests and headless apps. A backend that cannot start fails `Install` with a diagnostic, never falling back to null. GL and its loader stay in `src/Backend/OpenGL/`, linked `PRIVATE`.
- **Commands:** recorded through per-pass encoders into a FenrirRenderer-owned `CommandList` of plain structs, replayed by the backend on one virtual `Submit`; resource creation and destruction are virtual calls. Recording needs no GPU thread.
- **Handles:** typed index plus generation, owned by the device; destruction deferred by frame index.
- **Public or private:** the graphics device is private. A UI library depends on nothing in Fenrir; glue adapts it to a public overlay interface that must let a UI bring its own shaders and vertex layouts.
- **Refused:** native escape hatches, backend tuning settings. **Accepted:** behaviour differences where tests do not pin them; the device fixes [0, 1] depth and an upper-left origin. **Contained:** all GPU calls from one thread, which one is the frame ticket's call.
- **Revisit** if the command list's cost shows in profiling or under a render thread, or a second package needs raw GPU work.
