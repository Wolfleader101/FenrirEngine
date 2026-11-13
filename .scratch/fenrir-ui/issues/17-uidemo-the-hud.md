# 17: UIDemo: the HUD

**What to build:** a new example app whose HUD, over a lit room, shows what a game gets from the stack (spec: "UIDemo"; ADR-0052 for assets).

- **UIDemo** under `examples`, a lit room from existing demo assets with a controllable camera, scripting optional as the other demos have it, its switches listed on screen as LightingDemo does, a README naming each screen.
- **The HUD root** on the window overlay from a markup document and a stylesheet under the demo's assets: health and stamina bars (progress elements driven by a system from demo components), an ammo counter label, a compass as a C++ custom element painting strokes and ticks from the camera's yaw, a minimap image element showing a top-down camera's render target image, hit markers created on a key with `:intro` and removed with `:outro`; everything `pointer-events: none` so the camera still turns under the mouse.
- Hot reload on, with the stylesheet's path shown so a user can edit it and watch.

**Blocked by:** 04, 10, 15, 16

**Touches:** `examples/UIDemo/`, `examples/CMakeLists.txt`, `README.md`

**Status:** resolved

- [x] The demo builds and runs under the msvc-x64-debug preset with scripting off; the README says what each element shows and which file to edit.
- [x] A headless smoke test (null backend) drives the demo's HUD setup one frame and checks the bars' rectangles follow their components.

Hand check: the HUD draws over the room, the compass turns with the camera, the minimap shows the top-down view, hit markers animate in and out, editing the stylesheet's colours changes them live, and clicks still reach the camera.

- [x] `check_tickets.py` and clang-format pass.
