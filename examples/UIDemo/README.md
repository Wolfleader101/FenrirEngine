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

An ImGui window, as LightingDemo lists its switches: the room's keys, the HUD's and the menu's hot reload switches and
the full paths of their stylesheets (also logged at start), and the menu's volume and field of view as last set.

## Scenes

| Scene | Shows | Keys |
| --- | --- | --- |
| The HUD | A UI root on the main window's overlay, under the ImGui window, loaded from the markup document `assets/ui/hud.ui` and styled by `assets/ui/hud.css`, over a room of 14 by 14 metres with pillars, crates and three spheres, lit by a dim sun through its open top and three warm lamps. Top centre, the **compass**: a custom element (`src/Compass.cpp`, registered as `compass`) painting with the painter 120 degrees of heading round the camera's, ticks every 15 degrees, taller at north, east, south and west, a triangle over north and a needle at the centre, in the element's `color`. Top right, the **minimap**: an `image` element showing what an orthographic camera looking straight down draws into a render target image, following the camera across the room, north up, in a round frame clipped by `overflow: hidden`, with the player's arrow at its centre turning with the heading. Bottom left, the **health** and **stamina bars**: `progress` elements set from the components; stamina drains while the camera moves and refills while it stands. Bottom right, the **ammo counter**: a `label` reading the rounds in the magazine over the reserve. At the centre, a dot, and a **hit marker** for each shot, made by the game, scaling in through `:intro` and fading out through `:outro` a quarter of a second later. Nothing in the HUD takes pointer input (`pointer-events: none`), so the right mouse button still turns the camera over it and a left click still reaches the room. | F or left click: fire (a hit marker, one round down; nothing with the magazine empty). R: reload from the reserve (it refills once both are empty). H: take a hit (15 health; at zero it refills). Right mouse button and WASD: move, draining stamina. Escape: the pause menu. |

## The pause menu

Escape opens it: a second UI root on the window overlay (`MenuLayer`, over the HUD and under the ImGui window, with a
higher input priority than the HUD's), styled by `assets/ui/menu.css`. It is game code (ADR-0063): the engine keeps no
menu stack, and the `Menu` resource (`src/Menu.cpp`) decides what is there. Opening loads `assets/ui/menu.ui` into the
root; Settings loads `assets/ui/settings.ui` beside it; Back, B on a gamepad, or Escape steps out of the settings and
then out of the menu, and Resume closes it. Each panel comes in through `:intro` (moving up and growing) and leaves
through `:outro`, its elements removed once the outro ends; the backing that dims the room fades.

- The **panels** are frosted: `backdrop-filter: blur(14px)` blurs the room under them, under a dark tint. The blur is
  square-cornered (the renderer's backdrop composite takes no mask), so the panels' corners are only slightly round. They move rather than fade, because a fading element draws through an offscreen group, and a blur
  inside one would blur the group's image instead of the room.
- The **buttons** lighten and slide on hover, shrink while pressed, and show the focus ring (`:focus`), which Tab,
  Shift+Tab and the gamepad's D-pad and left stick move; A presses the focused control.
- The **settings**: sliders for the volume (kept and shown in the UIDemo window: the engine has no audio yet) and the
  window camera's field of view; checkboxes for invert look (the scaffold's `FreeFlyLook`), show HUD and the virtual
  cursor (the left stick moves a pointer over the menu and A clicks under it); a text entry for the player's name, which
  the HUD shows over the vitals; and a scrolling list of the controls.
- While it is open its backing covers the window and takes pointer input, so the right mouse button and clicks do not
  reach the room, and the room's keys do nothing: the camera stops turning and the game waits.
- The menu's system reads the root's UI events through `EventReader<UIEvent>` (ADR-0065): clicks by the buttons' ids,
  Change from the controls, Cancel from B, and Escape while one of its controls has the focus (the root then has the
  keyboard). It keeps the settings, and writes them back into the controls each time the settings panel loads, or a
  hot reload loads it again.

## The screen

A screen on the room's north wall, facing the camera's start, shows the menu in a texture: a third UI root, drawn into
an image (`UIImageTarget`, ScreenWidth by ScreenHeight, 1280 by 720) that the screen's unlit material shows, in a dark
bezel. It holds the menu's document from the start, styled by the same `menu.css`, in the linear blend space an image
root takes by default; its 1080 UI units span the image's 720 pixels, so it is drawn at the image's own resolution. It
takes no input. The menu's system mirrors the live menu into it each frame: its settings panel opens and closes with the
live one's, its controls show the kept settings, and its focus ring is on the element with the live focus's id. With the
menu closed, the screen shows the pause panel. Its root has hot reload on too, so an edit to the menu's files restyles
the screen as well.

## Editing the HUD and the menu while they run

The HUD root has hot reload on: save `examples/UIDemo/assets/ui/hud.css` and the HUD restyles within a quarter of a
second; save `hud.ui` and it loads the document again. The menu's root does the same for `menu.css`, and for `menu.ui`
and `settings.ui` while they are open. Its colours are the custom properties at the top of the sheet
(`--health`, `--stamina`, `--accent`, `--ink` and the rest). The files are read from the source tree while it is there
(`UIDEMO_SOURCE_ASSETS`), so an edit survives a rebuild; without it they are read from the asset root beside the
executable. The UIDemo window shows which. A version with an error is not taken: the one before stays, and the errors
are logged.

## Tests

`UIDemoTests` (CTest `UIDemo`) sets the app up as `main` does, on the null backend with no window, its HUD root drawing
into a capturing target of 1920 by 1080, and drives it a frame at a time: the health and stamina bars' fills cover the
share of their bars the player's health and stamina hold, the ammo counter reads the player's ammo, and a right press over
the HUD's elements hits none of them and reaches the game's `Mouse`. The menu root draws into a second capturing target:
Escape opens the menu, whose root then takes a right press away from the game; a gamepad direction and A click Resume,
which closes it, and once its outro ends the game has the mouse again; and a name typed into the settings panel's entry
shows on the HUD. The screen's root is checked through the same app: it holds the pause panel from the start, in an image
of 1280 by 720 at a scale of 720 / 1080, blending linear, and its settings panel, its name entry and its focus follow the
live menu's.

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
- Escape opens the pause menu over a dimmed room, blurred behind its panel; it moves in, and out again on Resume.
- Every control works with the mouse, with Tab and Enter, and with a controller (D-pad and A, B to back out), the focus
  ring showing where the focus is; with the virtual cursor on, the left stick moves a pointer and A clicks.
- The name typed in Settings shows on the HUD; the field of view slider widens and narrows the view; show HUD hides it;
  invert look turns the camera the other way up and down.
- The right mouse button does not turn the camera while the menu is open.
- Changing a colour in `menu.css` while the menu is open restyles it within a moment.
- The screen on the north wall shows the menu, crisp at its native size: the pause panel with the menu closed, and,
  with it open, the settings panel, the settings and the focus ring as the live menu has them.
