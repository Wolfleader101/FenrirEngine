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

## Hand checks

- RendererDemo opens a window showing the lit cube, three faces lit differently.
- Holding the right mouse button and moving the mouse looks around; with it held, WASD flies, Shift flies faster and the
  wheel changes the speed.
- PageDown and PageUp wrap the one-scene list: the scene is left and entered again (`Scene 1/1: Meshes and materials`
  is logged again).
- F1 logs the help (app, scene, keys). It does not show on screen until renderer ticket 17 draws debug screen text.
- F3 turns the free-fly camera off (the window is cleared to black, as with no camera) and on again.
