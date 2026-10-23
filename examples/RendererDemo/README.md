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
| 1 | Meshes and materials | A placeholder until renderer ticket 13 fills it: the shared cube (`cube.obj`) in `builtin:lit`, a directional light and the default ambient light, seen through the free-fly camera. | None yet. |
| 2 | Cameras and render targets | A ring of coloured boxes round a spinning white one, seen by its own cameras: a main view, a second view for split screen, an orthographic top-down minimap in the top-right corner over them, and a monitor whose screen shows what a third camera, circling the scene, draws into an image target. The monitor's own camera sees the screen black: a view does not sample its own target (logged once). | L: the layout, cycling one view, side by side and one over the other. O: the minimap's `order`, over or under the main views. P: the main views' projection, perspective or orthographic. |

Every scene draws with 4x MSAA (`DemoConfig::msaa`).

## Hand checks

- RendererDemo opens a window showing the lit cube, three faces lit differently.
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
