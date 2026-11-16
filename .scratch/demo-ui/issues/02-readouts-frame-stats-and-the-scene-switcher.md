# 02: Readouts, frame stats and the scene switcher

**What to build:** the demo panel shows live values and lets you change scene by clicking. A scene pushes readouts, an app can turn on a frame-stats section, and the panel's first section lists the app's scenes as buttons.

**Spec:** `.scratch/demo-ui/spec.md`, these sections:
- "The C++ API" (`Readout`, `Text`, `Separator`, row `SetShown`)
- "What the scaffold feeds the kit" (the scene switcher, `DemoConfig::frameStats`)

- **Readouts:**
  - `DemoSection::Readout(label)` returns a `DemoReadout` with `Set(std::string_view)`, which changes the element's text only when the value differs.
  - `Readout(label, std::function<std::string()>)` is pulled once a frame in the kit's build stage work.
  - A label column and a value column, laid out by `demo.css`.
- **Separators and row visibility:** `Separator(title)`, and `SetShown` and disabled on every row handle.
- **Frame stats:** `DemoConfig::frameStats` adds an app section, "Frame", with:
  - frame time and fps, smoothed over half a second
  - draws, instances and shadow draws from `Renderer::Stats`
- **The scene switcher:** the panel's first app section, "Scenes". One button per scene, the active one with the class `active`. A click enters that scene the next frame, exactly as PageDown would, with the same logging and smoke-run behaviour.
- **README:** DemoScaffold's README documents readouts, frame stats and the switcher.

**Blocked by:** 01

**Touches:** `examples/DemoScaffold/`

**Status:** ready-for-agent

- [ ] Scaffold tests on the null backend:
  - a readout set by a scene's `update` shows its value that frame, and a pulled readout shows its function's value
  - the scene switcher lists every scene, marks the active one, and a click on the third enters it
  - a scene's readouts are gone after it is left and fresh when it is entered again
  - `frameStats` rows show the renderer's draws on the null backend
- [ ] Every demo's `Smoke-null` passes with scripting off and on.
- [ ] Clang and MSVC x64 debug build without new warnings.
