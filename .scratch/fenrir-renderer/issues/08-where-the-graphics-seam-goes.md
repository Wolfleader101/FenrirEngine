# Where does the graphics seam go, and is the backend picked at compile time or at runtime?

Type: grilling
Status: open
Blocked by: 01, 02

## Question

Where does the line between FenrirRenderer and the graphics API sit, and how is the backend chosen? **ADR.**

- Is the seam a thin interface over GPU resources and commands (buffers, textures, pipelines, passes, command recording), a higher-level renderer with one implementation per API, or something between?
- Is the backend picked at compile time (as ADR-0010 did for physics: one private backend, no virtual calls) or at runtime (an interface, backends chosen at start-up)? What would make us revisit it?
- Is the seam public (an engine-level graphics layer game code or other packages may use, e.g. a UI library) or private to FenrirRenderer?
- How are GPU objects named across the seam (handles, lifetimes, deferred destruction)?
- What is refused (native escape hatches, backend tuning) and what is accepted (behaviour differences)?

Consult `codebase-design` for the seam.
