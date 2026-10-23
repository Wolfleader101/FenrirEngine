# 12: The demo scaffold and an empty RendererDemo

**What to build:** Demo apps are cheap: a static `FenrirDemoScaffold`, a `fenrir_add_demo_app` CMake function and smoke runs in CTest, proven by a RendererDemo whose one scene shows the lit cube under a free-fly camera (spec: "Demo apps" (scaffold, keys, smoke runs, assets), "Build" (examples options); ticket 23). Later feature tickets each add a RendererDemo scene.

- **`examples/DemoScaffold/`:** `FenrirDemoScaffold` and `fenrir_add_demo_app(<name> SOURCES … SCRIPTS … ASSETS …)`, which makes the executable, links the scaffold, copies assets into the app's asset root (a build-directory folder holding the shared `examples/assets/` and the app's own `examples/<App>/assets/`), and registers the smoke tests. Building and copying an app's C# script project, and the `Fenrir.Demo` assembly, are added by ticket 25; leave the `SCRIPTS` argument accepted.
- **`DemoScaffold::Install(App&, DemoConfig)`** installs Window, Input, Assets and Renderer (DebugDraw and Scripting are added by tickets 17 and 25) and adds:
  - a free-fly camera (right mouse to look, WASD, Shift, wheel) writing a camera entity's `Transform`; a scene that spawns its own camera turns it off until F3;
  - the ordered scene list, PageDown/PageUp stepping scenes (each scene spawned into its own FenrirScene and destroyed on leave);
  - a scene-click event carrying a ray (from the camera; ticket 14 switches it to `ScreenPointToRay`);
  - the F1 help panel's content (app, scene, the scene's keys) logged on F1 until ticket 17 draws it as screen text.
  - Keys: F1 help and F3 free-fly reserved by the scaffold, F2 the app's, every other key the scene's.
- **Smoke runs:** `FENRIR_DEMO_SCENE=<name>` and `FENRIR_DEMO_KEYS=1` keep their meaning; `FENRIR_DEMO_SMOKE=<frames>` runs every scene (or the named one) that many frames with its keys pressed and exits non-zero on any logged error or GL debug error; `FENRIR_DEMO_BACKEND=null|opengl`; `FENRIR_DEMO_AUTOPLAY` is not carried over. CTest gets one test per app per backend; the GL one runs on Linux CI's llvmpipe job.
- **Options:** `FENRIR_BUILD_EXAMPLES` (`ON`) now means the new demo apps; the legacy DemoApp moves behind `FENRIR_BUILD_LEGACY_DEMOAPP` (`OFF`), untouched apart from one guard: skip its own `add_subdirectory(assimp)` when an `assimp` target exists. CI's `FENRIR_CMAKE_OPTIONS` turns examples on; README options table updated.
- **Shared assets** in `examples/assets/`: a cube and a sphere to start (the backpack, art-deco textures and skybox come with the scenes that use them).
- **RendererDemo** (`examples/RendererDemo/`): one placeholder scene, "Meshes and materials", with a lit cube, a light and the free-fly camera; ticket 13 fills it.
- README for the scaffold and RendererDemo, with the keys and the smoke switches.

**Blocked by:** 11 (Renderer tracer bullet: a lit mesh drawn by a camera).

**Touches:** `examples/DemoScaffold/`, `examples/RendererDemo/`, `examples/assets/`, `examples/CMakeLists.txt`, `CMakeLists.txt`, `README.md`, `.github/workflows/build-and-test.yml`

**Status:** resolved

Hand check: RendererDemo opens a window showing the lit cube; the free-fly camera looks with the right mouse button held and flies with WASD, Shift and the wheel; PageDown/PageUp wrap the one-scene list (the scene is entered again); F3 turns the free-fly camera off and on; F1 logs the help (it shows on screen only once ticket 17 draws debug screen text).
Hand check: the legacy DemoApp still builds with `-DFENRIR_BUILD_LEGACY_DEMOAPP=ON` (its libraries are not in agent worktrees).

- [ ] `FENRIR_DEMO_SMOKE=60 FENRIR_DEMO_BACKEND=null` runs RendererDemo with no errors on every preset, and the GL smoke test passes on Linux llvmpipe; a deliberately logged error makes the smoke run exit non-zero.
- [x] CTest lists one RendererDemo test per backend.
- [ ] The legacy DemoApp still builds with `FENRIR_BUILD_LEGACY_DEMOAPP=ON` and is not built by default.
- [ ] RendererDemo opens a window with the lit cube; the free-fly camera moves and looks; PageUp/PageDown wrap the (one-scene) list; F1 logs the help (checked by hand).
