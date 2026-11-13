# UIDemo

What a game gets from the UI stack (`.scratch/fenrir-ui/spec.md`, "UIDemo"): FenrirUI's elements, stylesheets, markup
documents, transitions and custom elements, over a lit room, on the demo scaffold (`examples/DemoScaffold/README.md`,
for the keys every demo app shares and the smoke-run switches). It runs with scripting on or off; it has no C# of its
own yet.

```sh
cmake --build out/build/clang-x64-debug --target UIDemo
out/build/clang-x64-debug/examples/UIDemo/UIDemo.exe
FENRIR_DEMO_SMOKE=60 FENRIR_DEMO_BACKEND=null out/build/clang-x64-debug/examples/UIDemo/UIDemo.exe
```

The HUD is game code over the engine's UI (ADR-0063): the room keeps the player's `Health`, `Stamina` and `Ammo`
components, and its HUD system pushes what they hold into the HUD's elements every frame (ADR-0065). The code is in
`src/` (`UIDemoCore`, which the app and its test link), the room's assets come from the shared demo assets
(`examples/assets`: the cube, the sphere and Khronos's BoxTextured), and nothing is downloaded (ADR-0052).

## The UIDemo window

An ImGui window, as LightingDemo lists its switches: the room's keys, the HUD root's hot reload switch, and the full
path of the HUD's stylesheet, which is also logged at start.

## Screens

| Screen | Shows | Keys |
| --- | --- | --- |
| The HUD | A UI root on the main window's overlay, under the ImGui window, loaded from the markup document `assets/ui/hud.ui` and styled by `assets/ui/hud.css`, over a room of 14 by 14 metres with pillars, crates and three spheres, lit by a dim sun through its open top and three warm lamps. Top centre, the **compass**: a custom element (`src/Compass.cpp`, registered as `compass`) painting with the painter 120 degrees of heading round the camera's, ticks every 15 degrees, taller at north, east, south and west, a triangle over north and a needle at the centre, in the element's `color`. Top right, the **minimap**: an `image` element showing what an orthographic camera looking straight down draws into a render target image, following the camera across the room, north up, in a round frame clipped by `overflow: hidden`, with the player's arrow at its centre turning with the heading. Bottom left, the **health** and **stamina bars**: `progress` elements set from the components; stamina drains while the camera moves and refills while it stands. Bottom right, the **ammo counter**: a `label` reading the rounds in the magazine over the reserve. At the centre, a dot, and a **hit marker** for each shot, made by the game, scaling in through `:intro` and fading out through `:outro` a quarter of a second later. Nothing in the HUD takes pointer input (`pointer-events: none`), so the right mouse button still turns the camera over it and a left click still reaches the room. | F or left click: fire (a hit marker, one round down; nothing with the magazine empty). R: reload from the reserve (it refills once both are empty). H: take a hit (15 health; at zero it refills). Right mouse button and WASD: move, draining stamina. |

## Editing the HUD while it runs

The HUD root has hot reload on: save `examples/UIDemo/assets/ui/hud.css` and the HUD restyles within a quarter of a
second; save `hud.ui` and it loads the document again. Its colours are the custom properties at the top of the sheet
(`--health`, `--stamina`, `--accent`, `--ink` and the rest). The files are read from the source tree while it is there
(`UIDEMO_SOURCE_ASSETS`), so an edit survives a rebuild; without it they are read from the asset root beside the
executable. The UIDemo window shows which. A version with an error is not taken: the one before stays, and the errors
are logged.

## Tests

`UIDemoTests` (CTest `UIDemo`) sets the app up as `main` does, on the null backend with no window, its HUD root drawing
into a capturing target of 1920 by 1080, and drives it a frame at a time: the health and stamina bars' fills cover the
share of their bars the player's components hold, the ammo counter reads the player's ammo, and no element of the HUD
takes pointer input.

## Hand checks

On OpenGL, in The HUD:

- The HUD draws over the room: the compass at the top, the round minimap at the top right, the bars at the bottom left
  and the ammo counter at the bottom right.
- Turning the camera (right mouse button held) turns the compass's ticks the other way, north's triangle passing the
  needle when the camera faces -Z, and turns the minimap's arrow.
- The minimap shows the room from above, and moves with the camera as it flies (WASD).
- F and left clicks show hit markers that scale in and fade out; the ammo counter counts down, and R reloads it.
- H shortens the health bar; flying drains the stamina bar, which refills once the camera stops.
- Changing `--health` in `hud.css` while the demo runs recolours the health bar within a moment, without a restart.
- Clicks and the right mouse button over the HUD's elements still reach the room and the camera.
