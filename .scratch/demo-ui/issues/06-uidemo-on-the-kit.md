# 06: UIDemo on the kit

**What to build:** UIDemo's ImGui switches window becomes the demo panel. Its keys become key rows and the room gets a tutorial. UIDemo then sets no `imguiOverlay`, and its HUD, menu, screen and world roots behave as before under the kit.

**Spec:** `.scratch/demo-ui/spec.md`, "Per-demo moves" (UIDemo). The inventory is `.scratch/demo-ui/research/inventory.md`.

- **`Room.cpp`'s `ShowSwitches` becomes an app section, "UIDemo":**
  - toggles for the HUD's and the menu's hot reload (`SetHotReload`)
  - `Text` rows with their stylesheet paths (wrapped)
  - readouts for the volume ("no audio yet") and the field of view, as last set
- **Keys and tutorial:**
  - The six key lines become the room's `keyRows`.
  - A tutorial for the room, four or five steps: fire, reload, take a hit, open the menu and use it with mouse, keys and pad, the light terminal. A step may end itself on the event it teaches.
- **`DemoConfig.cpp`** drops `imguiOverlay = true` and its comment.
- **`UIDemo.hpp`'s layers:** `HudLayer`, `MenuLayer` and `MenuInputPriority` lose their "under ImGui's" wording and say they are under the kit's (`DemoUILayer`).
- **The terminal's ray check** in `Room.cpp` keeps reading the window's `pointer` claim, which now includes the kit's panel.
- **README:**
  - "The UIDemo window" becomes "The demo panel".
  - The scene table's keys match the key rows.
  - The hand checks gain the panel's toggles and the tutorial, and lose the ImGui window.
- **Tests:** UIDemo's tests keep passing, and the "under the ImGui window" wording goes from the README's Tests section.

**Blocked by:** 05

**Touches:** `examples/UIDemo/`

**Status:** ready-for-agent

- [ ] No ImGui call or include remains in `examples/UIDemo/`, and `DemoConfig.cpp` sets no `imguiOverlay`.
- [ ] `UIDemo` tests and `UIDemo-Smoke-null` pass with scripting off and on.
- [ ] Hand checks, on OpenGL:
  - the panel's hot reload toggles turn reloading off and on
  - the paths show
  - the volume and field of view follow the menu's settings
  - the tutorial shows once and steps
  - the terminal takes no ray while the pointer is over the panel
  - the menu still opens over the HUD, and the kit's panel stays over both
- [ ] Clang and MSVC x64 debug build without new warnings.
