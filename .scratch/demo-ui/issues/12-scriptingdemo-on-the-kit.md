# 12: ScriptingDemo on the kit

**What to build:** ScriptingDemo's Showcase gets key rows and a tutorial. Its README describes the Razor panel ticket 05 gave it.

**Spec:** `.scratch/demo-ui/spec.md`, "Per-demo moves" (ScriptingDemo).

- **Showcase's key help** becomes key rows: R, Space, G, and any key ticket 05 added.
- **A tutorial** for Showcase, three or four steps: the scene list, F1, the C# systems' keys, and the panel being a Razor component with hot reload of the scripts.
- **README:** a "The demo panel" section (the Razor panel, its switches and their keys, the scripting-off note) and rewritten hand checks.

**Blocked by:** 05

**Touches:** `examples/ScriptingDemo/`

**Status:** resolved

- [x] `ScriptingDemo-Smoke-null` passes with scripting on; with scripting off ScriptingDemo is not built (`examples/CMakeLists.txt`), and the rest builds and passes.
- [ ] Hand checks, on OpenGL:
  - the help lists the key rows
  - the tutorial shows once and steps
  - rebuilding the scripts while the demo runs brings the Razor panel back after the reload
- [ ] Clang and MSVC x64 debug build without new warnings. (MSVC x64 debug verified, scripting on and off; clang not built here.)
