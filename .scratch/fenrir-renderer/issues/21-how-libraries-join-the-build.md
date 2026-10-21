# How do the new libraries join the build?

Type: grilling
Status: open
Blocked by: 09, 12, 15

## Question

How do the windowing library, the GL loader, the asset importer(s), the shader toolchain and any animation library join the build, following "How does Jolt join the build?" on the physics map?

- Submodules or another route, pinned versions, static linking, `PRIVATE` links, options turned off.
- Engine warning flags kept off third-party code; MSVC runtime; Debug and Release.
- CMake options (`FENRIR_ENABLE_RENDERER`?) and defaults; what a build with rendering off looks like.
- What happens to `examples/DemoApp/libs` (not submodules today).
