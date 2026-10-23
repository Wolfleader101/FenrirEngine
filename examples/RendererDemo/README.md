# RendererDemo

The renderer's demo app, on the demo scaffold (`examples/DemoScaffold/README.md`, for the keys every demo app shares and
the smoke-run switches). Each renderer feature ticket adds a scene with its hand checks.

```sh
cmake --build out/build/clang-x64-debug --target RendererDemo
out/build/clang-x64-debug/examples/RendererDemo/RendererDemo.exe
FENRIR_DEMO_SMOKE=60 FENRIR_DEMO_BACKEND=null out/build/clang-x64-debug/examples/RendererDemo/RendererDemo.exe
```

## Scenes

| # | Scene | Shows | Keys |
| --- | --- | --- | --- |
| 1 | Meshes and materials | The imported backpack (`SpawnModel`); a cube and a sphere in `builtin:unlit`, then in `builtin:lit`; a cube in RendererDemo's own stripes shader (`shaders/stripes.*`); a cube with no material (the magenta fallback); an empty mesh and a stale one (nothing drawn); three cubes sharing one material; a metallic/roughness grid of spheres behind; the four blend modes side by side over a checkerboard (Opaque, Masked, AlphaBlend, Additive, each a disc fading to its edge); and a double-sided quad turning beside a single-sided one. | L: turn the light 45 degrees. M: the ambient light, dim, bright, then off. V: hide or show the backpack (`visible`). C: the colour of the three cubes' shared material. |
| 2 | Cameras and render targets | A ring of coloured boxes round a spinning white one, seen by its own cameras: a main view, a second view for split screen, an orthographic top-down minimap in the top-right corner over them, and a monitor whose screen shows what a third camera, circling the scene, draws into an image target. The monitor's own camera sees the screen black: a view does not sample its own target (logged once). | L: the layout, cycling one view, side by side and one over the other. O: the minimap's `order`, over or under the main views. P: the main views' projection, perspective or orthographic. |

Every scene draws with 4x MSAA (`DemoConfig::msaa`).

## Hand checks

- RendererDemo opens a window on Meshes and materials (`Scene 1/2: Meshes and materials` is logged):
  - The backpack on the left is textured (its diffuse and normal maps) and lit, its bumps catching the light.
  - The blue unlit cube and sphere are flat, one colour all over; the lit ones beside them are shaded, with a highlight.
  - The striped cube's diagonal yellow and black stripes scroll.
  - The cube after it is flat magenta, and the gap after that holds nothing: the empty and the stale mesh (a stale mesh
    and the missing material are logged once each).
  - In the grid behind, metallic grows from left to right (the spheres darken and take on the gold in their
    highlights) and roughness from bottom to top (the highlight spreads from a sharp point to nothing).
  - Over the checkerboard: the red Opaque square ignores its texture's alpha; the green Masked disc has a hard edge
    with the checkerboard showing round it; the blue AlphaBlend disc fades softly into the checkerboard; the orange
    Additive disc glows, brightening what is behind it.
  - Of the two purple quads turning on the right, the double-sided one shows its back as it turns, lit as its own
    side; the single-sided one vanishes for the half turn it faces away.
  - L turns the light: the shading of the lit objects and the grid's highlights move. M makes the shadowed sides
    brighter, then black. V hides the backpack and shows it again. C changes the three cubes' colour together.
- Holding the right mouse button and moving the mouse looks around; with it held, WASD flies, Shift flies faster and the
  wheel changes the speed.
- PageDown and PageUp step through the two scenes, wrapping at both ends (`Scene 2/2: Cameras and render targets` and
  `Scene 1/2: Meshes and materials` are logged).
- F1 logs the help (app, scene, keys). It does not show on screen until renderer ticket 17 draws debug screen text.
- F3 turns the free-fly camera off (the window is cleared to black, as with no camera) and on again.
- Cameras and render targets (PageDown from the first scene, or `FENRIR_DEMO_SCENE="Cameras and render targets"`):
  - The main view fills the window, with the minimap, an orthographic top-down view of the whole ring with no
    perspective, in the top-right corner, on its dark blue background.
  - The monitor at the back shows the scene from the circling third camera, upright and moving, with the spinning box
    and the ring in it; the screen inside the monitor's own view is black.
  - L splits the window into the main view and the second view (from the right of the ring) side by side, then one over
    the other, then back to one view. Each view fills its half exactly, with no gap, overlap or stretching.
  - O sends the minimap under the main views (it disappears) and brings it back over them.
  - P makes the main views orthographic (parallel edges, no foreshortening) and perspective again.
  - Resizing the window keeps every viewport proportional: the split halves stay halves, and the minimap stays in its
    corner at the same fraction of the window.
  - Box edges are smooth, not stair-stepped (4x MSAA).
