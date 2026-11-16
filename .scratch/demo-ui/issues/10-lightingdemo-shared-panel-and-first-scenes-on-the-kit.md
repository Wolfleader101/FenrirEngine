# 10: LightingDemo's shared panel and its first scenes on the kit

**What to build:** LightingDemo's ImGui "Lighting" window becomes kit sections that every scene shares, with every row, range, logarithmic flag and key it has today. Material grid, Showcase models, Showroom and Many lights (C++ and C#) move their scene sections onto the kit, Many lights (C#)'s as a Razor panel. The first scene and Many lights get tutorials.

**Spec:** `.scratch/demo-ui/spec.md`, "Per-demo moves" (LightingDemo), "The controls FenrirUI lacks are kit composites" (ADR-0071). The inventory is `.scratch/demo-ui/research/inventory.md`, "LightingDemo".

- **`LightingPanel.hpp/.cpp`:**
  - `RunLightingPanel` stops drawing ImGui. A `LightingPanel` adds the shared sections once per scene entry over the scene's `LightingSettings`, bound through `DemoValue`.
  - `ApplyKeys` and `Apply` stay as they are, so the keys and the panel move the same values.
  - The sections and their rows:
    - **Camera:** exposure; the tonemapper as a `Choice`; bloom, streak and flare, the logarithmic ones logarithmic; lens dirt.
    - **Colour grade:** temperature (log), tint, contrast (log), the shadow and highlight tints as `Colour` rows, balance, saturation, and "No grade" as a `Button`.
    - **Camera effects.**
    - **Ambient occlusion.**
    - **Reflections:** "Show reflections" or "Show confidence" as a `Choice` of two.
    - **Sun shadows.**
    - **Debug view:** a `Choice` stepper of 8, with the light-cluster legend as `Text` shown only for that view.
    - **Sky:** the panorama as a `Choice`, or a `Text` "No panorama loaded"; intensity; rotation as an angle `Range`.
  - `LightingKeyHelp` becomes key rows.
- **The scenes' own sections:**
  - **Material grid:** the sun toggle and the base colour as a `Colour` row.
  - **Showcase models:** the turntable.
  - **Showroom:** per rect light, a separator with its name, then width, height, intensity and two-sided.
  - **Many lights:** lights as an integer range, moving, intensities, range, the shadow toggles and budgets, clustered, and the text lines as readouts.
- **Many lights (C#):** its C++ `update` stops running a panel. Its C# side (`scripts/ManyLights.cs`) mounts a Razor panel (`razorPanel`) of its own switches, all on its keys (M, P, K, J and V).
- **The shared sections** still show for the large scenes, which ticket 11 moves. Until then those scenes keep their ImGui sections, so `imguiOverlay` stays on in `main.cpp`.
- **Tutorials:**
  - The first scene: the lighting panel, the keys T, E, B, O, G, H and S, the scene list and F1.
  - Many lights: light counts, shadows and their budgets, and clusters.
- **README:** a "The demo panel" section for the shared sections and these scenes; their hand checks rewritten.

**Blocked by:** 05

**Touches:** `examples/LightingDemo/src/LightingPanel.hpp`, `examples/LightingDemo/src/LightingPanel.cpp`, `examples/LightingDemo/src/MaterialGrid.cpp`, `examples/LightingDemo/src/ShowcaseModels.cpp`, `examples/LightingDemo/src/Showroom.cpp`, `examples/LightingDemo/src/ManyLights.cpp`, `examples/LightingDemo/scripts/`, `examples/LightingDemo/README.md`

**Status:** ready-for-agent

- [ ] No ImGui call remains in `LightingPanel.cpp`, `MaterialGrid.cpp`, `ShowcaseModels.cpp`, `Showroom.cpp` or `ManyLights.cpp`.
- [ ] `LightingDemo-Smoke-null` passes with scripting off and on.
- [ ] Hand checks, on OpenGL:
  - every shared row changes the image as its ImGui slider did, at the same ranges
  - the logarithmic rows move logarithmically
  - T, E, B, O, G, H and S move the rows
  - the tints' swatches show their colours
  - each moved scene's rows work
  - Many lights (C#)'s Razor panel works
  - the tutorials show once and step
- [ ] Clang and MSVC x64 debug build without new warnings.
