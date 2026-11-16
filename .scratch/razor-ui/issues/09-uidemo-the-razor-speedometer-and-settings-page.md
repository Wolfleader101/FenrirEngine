# 09: UIDemo: the Razor speedometer and settings page

**What to build:** UIDemo's C# variant is written in Razor. The speedometer is one `.razor` file, and a Razor settings page lives inside the C++ pause menu (spec: "UIDemo"; ADR-0067 to ADR-0070).

- **`Speedometer.razor`** and **`Speedometer.razor.css`** replace `Speedometer.cs`'s panel, inline styles and paint system:
  - a readout label shows the smoothed speed;
  - `BuildHash` covers the speed rounded to the readout's precision;
  - `OnPaint` draws the gauge.

  A setup system reads the camera's speed as now, and mounts the component on the HUD root from `OnCreate`, and again when the root has lost it.
- **`Settings.razor`**, with its child components and `Settings.razor.css`, mounts into a `razor-settings` slot element added to the pause menu's markup. It has:
  - `@bind` on a volume slider, a subtitles checkbox and a player-name text entry;
  - a keyed `@foreach` list of key bindings with add and remove buttons;
  - `<Document Src>` placing a small credits document;
  - a child component with `ChildContent`;
  - a scoped sheet using `::deep`.
- `UIDemo.Scripts.csproj` imports `Fenrir.UI.Razor.props` (through `Fenrir.UI.Razor.InRepo.props`), and `MenuClicks.cs` stays.
- The demo's README describes the Razor variant, what each part proves, and how to edit `Settings.razor.css` live and rebuild the `.razor` files for a reload.

**Blocked by:** 06, 07, 08

**Touches:** `examples/UIDemo/`

**Status:** resolved

- [ ] UIDemo builds with scripting on, and the speedometer draws and reads out the speed as before.
- [ ] The settings page shows inside the pause menu:
  - its controls change the bound fields (logged);
  - rows add and remove with their outros;
  - the credits document shows.
- [ ] Editing `Settings.razor.css` while the demo runs restyles the page, and rebuilding the scripts brings the page back after the reload.
- [x] UIDemo still builds and runs with scripting off, unchanged.
- [x] Everything passes under the msvc-x64-debug preset. `check_tickets.py` and clang-format pass.
