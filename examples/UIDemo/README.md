# UIDemo

What a game gets from the UI stack (ADR-0060 to ADR-0066): FenrirUI's elements, stylesheets, markup
documents, transitions and custom elements, over a lit room, on the demo scaffold (`examples/DemoScaffold/README.md`,
for the keys every demo app shares and the smoke-run switches). It runs with scripting on or off; with it on, C# scripts
add a Razor speedometer to the HUD and a Razor settings page to the pause menu, and log the menu's clicks ("The C#
variant").

```sh
cmake --build out/build/clang-x64-debug --target UIDemo
out/build/clang-x64-debug/examples/UIDemo/UIDemo.exe
FENRIR_DEMO_SMOKE=60 FENRIR_DEMO_BACKEND=null out/build/clang-x64-debug/examples/UIDemo/UIDemo.exe
```

The HUD is game code over the engine's UI (ADR-0063): the room keeps the player's `Health`, `Stamina` and `Ammo`
components, and its HUD system pushes what they hold into the HUD's elements every frame (ADR-0065). The code is in
`src/` (`UIDemoCore`, which the app and its test link), the room's assets come from the shared demo assets
(`examples/assets`: the cube, the sphere and Khronos's BoxTextured), and nothing is downloaded (ADR-0052).

## The demo panel

The demo UI kit's panel, on the right (`examples/DemoScaffold/README.md`, "The demo UI kit"), has the app's section
"UIDemo": toggles for the HUD's and the menu's hot reload, each over the full path of its stylesheet (also logged at
start), and readouts of the menu's volume ("no audio yet") and field of view as last set. The room's keys are its key
rows in the help, at the bottom left, and the room has a demo tutorial, shown the first time it is entered: fire,
reload, take a hit, the light terminal, and last the pause menu, as the Escape that would open the menu closes the card
first. The first four steps end on what they teach. While the tutorial shows, Escape is the card's, not the menu's.

## Scenes

| Scene | Shows | Keys |
| --- | --- | --- |
| The HUD | A UI root on the main window's overlay, under the demo UI kit's panels, loaded from the markup document `assets/ui/hud.ui` and styled by `assets/ui/hud.css`, over a room of 14 by 14 metres with pillars, crates and three spheres, lit by a dim sun through its open top and three warm lamps. The compass's strip and the ammo counter's box cast soft shadows under them (`filter: drop-shadow()`, drawing each with its subtree through an offscreen group). Top centre, the **compass**: a custom element (`src/Compass.cpp`, registered as `compass`) painting with the painter 120 degrees of heading round the camera's, ticks every 15 degrees, taller at north, east, south and west, each of those four with its letter over its tick, and a needle at the centre, in the element's `color`. The letters are paragraphs the element lays out and paints itself, in `ui-sdf`, the engine's embedded face registered in SDF mode (ADR-0062), so they stay smooth at any size. Top left, clear of the demo panel, the **minimap**: an `image` element showing what an orthographic camera looking straight down draws into a render target image, following the camera across the room, north up, in a round frame clipped by `overflow: hidden`, with the player's arrow at its centre turning with the heading. Bottom right, beside the ammo counter and clear of the help, the **health** and **stamina bars**: `progress` elements set from the components; stamina drains while the camera moves and refills while it stands. Bottom right, the **ammo counter**: a `label` reading the rounds in the magazine over the reserve. At the centre, a dot, and a **hit marker** for each shot, made by the game, scaling in through `:intro` and fading out through `:outro` a quarter of a second later, its word HIT glowing through a blurred `text-shadow`. While the pause menu is open the room's HUD system gives the HUD's top element the class `paused`, and `filter: blur(3px)`, transitioned from `blur(0px)` over 0.3 seconds, blurs the whole HUD in behind the menu and out again as it closes. Nothing in the HUD takes pointer input (`pointer-events: none`), so the right mouse button still turns the camera over it and a left click still reaches the room. | F or left click: fire (a hit marker, one round down; nothing with the magazine empty). R: reload from the reserve (it refills once both are empty). H: take a hit (15 health; at zero it refills). Right mouse button and WASD: move, draining stamina. Escape: the pause menu (B on a gamepad backs out). Aim and left click: the light terminal's buttons and slider set the lamps. |

## The pause menu

Escape opens it: a second UI root on the window overlay (`MenuLayer`, over the HUD and under the demo UI kit, with a
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
- The **settings**: sliders for the volume (kept and shown in the demo panel: the engine has no audio yet) and the
  window camera's field of view; checkboxes for invert look (the scaffold's `FreeFlyLook`), show HUD and the virtual
  cursor (the left stick moves a pointer over the menu and A clicks under it); a text entry for the player's name, which
  the HUD shows over the vitals; and a scrolling list of the controls, whose bar is thin (`scrollbar-width: thin`) and
  in the accent colour over a faint track (`scrollbar-color`).
- The pause panel's title carries Fenrir's icon from the shared demo assets at its right end, in the panel's top-right
  corner: `background-size: contain` with `background-position: right center`.
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

## The world roots

Two more UI roots are on quads in the room (ADR-0060's world-space 2D target): each a `UIWorldTarget` over an entity the
room places, with the renderer's `WorldQuad2D` giving its size in metres and 500 UI units a metre, drawn in the camera's
`Transparent` pass among the transparent items, in the linear blend space, styled by `assets/ui/world.css` (hot reload
on), their text in the `ui-sdf` family. `WorldUI` (`src/World.cpp`) makes them when the room is spawned. In the C#
variant a third, the C# sign, is made from C# beside the name plate ("The C# variant", below).

- **The light terminal** (`assets/ui/terminal.ui`): a rounded glass panel of 1.6 by 1 metres on a post right of the
  camera's start, turned towards it and tilted back. Its buttons, Warm, Cool and Hue, set the colour of the room's three
  lamps, and its slider turns the hue Hue sets; a swatch and a reading show the colour. Its glass is laid out over its
  whole root by `inset: 0`; the reading's text and the swatch take the lamps' colour as their `color`, with a short
  transition, and the swatch's border, given no colour, is `currentcolor`, so it follows the reading. It is hit by a ray: each frame
  the room's system casts one from the window's camera through the cursor (`Renderer::ScreenPointToRay`) and gives it
  to the root with the left button's state (`UIRoot::SetPointerRay`), and the root hovers, presses, clicks and drags
  where the ray meets its quad. The window's mouse is never its own: it gets no ray while the menu is open, while the
  demo UI kit's panels or a window root have the pointer (the window's `pointer` claim), or while the right mouse button turns the camera. A click on it is not
  a shot. A world root never sets the window's cursor, so while the room's system gives it a ray it applies the
  terminal's `GetHoveredCursor()` to the window itself (the hand over its buttons and slider), and the arrow the frame
  the ray stops; the menu's buttons show the hand and its name entry the I-beam from the default stylesheet, set by
  their window root. Its corners and its content are cut by the painter's per-pixel clip, so they stay exact as the quad turns.
- **The name plate** (`assets/ui/plate.ui`): over a capsule left of the camera's start, a plate of 1 by 0.32 metres
  reading "Wanderer", which the room turns about its vertical axis to face the camera each frame. It takes no input.
  Its frame is `assets/ui/plate-frame.png`, a 96-pixel rim and glow nine-sliced over it by the `border-image` shorthand,
  its 40-pixel corners matching the plate's radius.

## The C# variant

With scripting built (`FENRIR_ENABLE_SCRIPTING`), the app runs `scripts/` (`UIDemo.Scripts.dll`) with the text, painter
and UI scripting extensions (ADR-0061), all of it C# over the generated Fenrir.UI and Fenrir.Painter contracts. Most of
it is Razor components (ADR-0067 to ADR-0070; `packages/FenrirUI/README.md`, "Razor components"): the scripts project
imports `Fenrir.UI.Razor.InRepo.props`, so its `.razor` files are compiled by the SDK's Razor generator and its
`.razor.css` files are their scoped sheets.

- **The speedometer** (`scripts/Speedometer.razor`, `Speedometer.razor.css`, `SpeedometerSetup.cs`): a Razor component
  on the HUD. An Update system finds the HUD root C++ made (`UIRoot.Find` on the main window at `HudLayer`) and mounts
  it there from `OnCreate`, and again whenever the root has lost it; each frame it gives it the window camera's speed,
  smoothed, from how far the camera moved since the last frame. Its host panel, bottom right over the ammo counter, is
  styled by its scoped sheet through its readable class, `.speedometer`, in place of inline styles. Its readout label
  shows the speed in metres a second, and its `BuildHash` is the speed rounded to the readout's one decimal, so it
  rebuilds only when the readout changes. Its `OnPaint` paints the gauge into the host every frame in the UI's paint
  stage, through the Painter facade: a dark disc, a track of three quarters of a turn, the speed's share of it (full at
  20 metres a second) and a needle. The gauge is the host's content, so the readout draws over it. Logged once: "the
  speedometer is on the HUD, a Razor component painted from C#".
- **The Razor settings page** (`scripts/Settings.razor`, `Settings.razor.css`, `Section.razor`, `RazorSettings.cs`): a
  card beside the pause panel, in the `razor-settings` slot of `assets/ui/menu.ui`. The slot is `display: none` in
  `menu.css` until the script mounts the page into it and gives it the class `filled`, so without scripting the menu is
  as it was. The menu loads its document each time it opens, so the script mounts the page each time it finds a slot
  the page is not in; what the page edits, a `DemoSettings`, is the script's and outlives it. It shows:
  - `@bind` on a volume slider, a subtitles checkbox and a player-name text entry, each logging its new value through
    `@bind:after` ("UIDemo (Razor): the volume is 40"). The engine has no audio or subtitles: the values are only kept.
  - A keyed `@foreach` list of key bindings (`@key` by each binding's id), with Add and Remove buttons (`@onclick`). A
    row comes in from the left through `:intro` and leaves to the right through `:outro`, and the rows around it keep
    their elements.
  - `<Document Src="ui/credits.ui" />` placing a small markup document, `assets/ui/credits.ui`, under a heading.
  - `Section`, a child Razor component with a title and `ChildContent`: the key bindings and the credits are each written
    inside a `<Section>` tag.
  - A scoped sheet: `Settings.razor.css` matches only the page's own elements, its `ChildContent` among them, and
    reaches Section's headings and the credits document's labels with `::deep`. The page's controls take `menu.css`'s
    unscoped rules too, so they look like the menu's own.
- **The C# sign** (`scripts/SignSetup.cs`, `assets/ui/sign.ui`): a small sign of 0.6 by 0.3 metres beside the name
  plate, facing the camera's start, made wholly from C#. The script makes its entity with a `Transform` and a
  `WorldQuad2D` (500 UI units a metre, as the other world roots), then, once the quad is there, a world root on it
  (`UIRoot.CreateWorldRoot`, hot reload on) holding its own markup document under `world.css`'s `#sign` rules: a title
  and one button. Each frame it gives the root a ray from the window's camera through the cursor
  (`Renderer.ScreenPointToRay`) with the left button's state (`UIRoot.SetPointerRay`), under the terminal's rules: no
  ray (`ClearPointerRay`) while the menu is open, while a window root has the pointer, or while the right mouse button
  turns the camera. A click on its button logs "UIDemo (C#): the C# sign was clicked", and is not a shot: the room
  counts a click over any world root's element as that root's. The root ends when the script
  generation is released, and the system destroys the quad's entity as it goes, so a reload makes a new sign. Logged
  once: "the C# sign is beside the name plate, a world root made from C#".
- **The menu's clicks** (`scripts/MenuClicks.cs`): an Update system reads `UIEvent`s through `EventReader<UIEvent>`,
  the queue the C++ `Menu` reads too, and logs each click on the menu root by the id of the element clicked or its
  nearest ancestor with one: "the menu's resume was clicked", and settings, quit, back and the settings' controls. A
  click in the Razor page logs as the slot's, `razor-settings`, beside the page's own handler.

### Editing the Razor components while they run

The menu's and the HUD's roots have hot reload on, and a Razor component's sheet is loaded from the path the build recorded,
the file in `examples/UIDemo/scripts/`. So with the menu open, save `Settings.razor.css` (try `.binding:outro`'s
`transform`, or `.page ::deep .heading`'s `color`) and the page restyles within a quarter of a second, scoped as before.
`Speedometer.razor.css` restyles the gauge the same way. A sheet edited while the demo is closed is recorded into the
scripts at the next build.

A `.razor` file is code: edit `Settings.razor` (a label's text, another binding in `RazorSettings.cs`'s spares) and
rebuild only the scripts while the demo runs:

```sh
cmake --build out/build/clang-x64-debug --target UIDemoDeployScripts
```

The demo notices the new build and reloads it between frames (`Scripting: reloaded 'UIDemo.Scripts.dll'`): the old
scripts' Razor components are unmounted, their elements removed at once, and the new scripts mount theirs, the
speedometer from `OnCreate` and the settings page as soon as the menu has its slot. The kept settings are the old
scripts' and go with them, so the page starts again from `DemoSettings`'s defaults. Smoke runs turn hot reload off.

## Editing the HUD and the menu while they run

The HUD root has hot reload on: save `examples/UIDemo/assets/ui/hud.css` and the HUD restyles within a quarter of a
second; save `hud.ui` and it loads the document again. The menu's root does the same for `menu.css`, and for `menu.ui`
and `settings.ui` while they are open. Its colours are the custom properties at the top of the sheet
(`--health`, `--stamina`, `--accent`, `--ink` and the rest). The files are read from the source tree while it is there
(`UIDEMO_SOURCE_ASSETS`), so an edit survives a rebuild; without it they are read from the asset root beside the
executable. The demo panel shows which. A version with an error is not taken: the one before stays, and the errors
are logged.

## Tests

`UIDemoTests` (CTest `UIDemo`) sets the app up as `main` does, on the null backend with no window, its HUD root drawing
into a capturing target of 1920 by 1080, and drives it a frame at a time: the health and stamina bars' fills cover the
share of their bars the player's health and stamina hold, the ammo counter reads the player's ammo, and a right press over
the HUD's elements hits none of them and reaches the game's `Mouse`. The menu root draws into a second capturing target:
Escape opens the menu, whose root then takes a right press away from the game, and the HUD is blurred by 3 while it is open and sharp again once it closes; a gamepad direction and A click Resume,
which closes it, and once its outro ends the game has the mouse again; and a name typed into the settings panel's entry
shows on the HUD. The screen's root is checked through the same app: it holds the pause panel from the start, in an image
of 1280 by 720 at a scale of 720 / 1080, blending linear, and its settings panel, its name entry and its focus follow the
live menu's. The world roots are checked there too: the terminal and the name plate are 800 by 500 and 500 by 160 UI
units at a scale of 1, blending linear, with `ui-sdf` registered in SDF mode; the terminal's glass covers its root by
`inset: 0`, once Cool's transition ends its swatch's border is the reading's colour, and the plate's frame loads by the
`border-image` shorthand; a ray at Cool and then Warm turns every
lamp that colour; and the hue slider, dragged by the ray, turns them to its hue. The demo UI kit's root draws into a
third capturing target of 1920 by 1080: the panel's UIDemo section shows both stylesheets' paths and the settings'
volume and field of view, which follow the settings panel's sliders; its toggles turn the HUD's and the menu's hot
reload off and on; the help lists the room's keys; the kit's panel and help cover none of the HUD's parts; the room's
tutorial shows on its first entry and steps on a shot, a reload and a hit; and the Escape that closes it does not open
the menu.

## Hand checks

On OpenGL, in The HUD:

- The HUD draws over the room: the compass at the top, the round minimap at the top left, and the bars and the ammo
  counter at the bottom right. The demo panel on the right and the help at the bottom left cover none of them.
- The help lists the scaffold's keys, then the room's: F or left click, R, H, the right mouse button and WASD, Escape,
  and aiming at the terminal. F1 hides the panel, the help and the tutorial card, leaving the HUD, and F1 again shows
  them and logs the help.
- The tutorial card shows at the top centre on the first entry only: a shot ends Fire, R ends Reload, H ends Take a
  hit, a click on the terminal ends The light terminal, and Done on The pause menu closes it. Escape closes it without
  opening the menu, and the panel's Tutorial button shows it again from the start.
- The demo panel's HUD hot reload and Menu hot reload toggles turn reloading off (an edit to `hud.css` or `menu.css`
  then shows nothing) and on again (it shows). The paths under them are the stylesheets', wrapped in the panel.
- The panel's Volume and Field of view follow the settings panel's sliders as they move.
- Turning the camera (right mouse button held) turns the compass's ticks the other way, its N passing the needle when
  the camera faces -Z, and turns the minimap's arrow. The compass's letters are smooth-edged.
- The minimap shows the room from above, and moves with the camera as it flies (WASD).
- The compass's strip and the ammo counter's box cast soft shadows down onto the room.
- F and left clicks show hit markers that scale in and fade out, their HIT glowing blue; the ammo counter counts down,
  and R reloads it.
- H shortens the health bar; flying drains the stamina bar, which refills once the camera stops.
- Changing `--health` in `hud.css` while the demo runs recolours the health bar within a moment, without a restart.
- Clicks and the right mouse button over the HUD's elements still reach the room and the camera.
- Escape opens the pause menu over a dimmed room, blurred behind its panel; it moves in, and out again on Resume. The
  HUD blurs in as the menu opens and clears as it closes.
- Every control works with the mouse, with Tab and Enter, and with a controller (D-pad and A, B to back out), the focus
  ring showing where the focus is; with the virtual cursor on, the left stick moves a pointer and A clicks.
- The name typed in Settings shows on the HUD; the field of view slider widens and narrows the view; show HUD hides it;
  invert look turns the camera the other way up and down.
- The right mouse button does not turn the camera while the menu is open.
- Changing a colour in `menu.css` while the menu is open restyles it within a moment.
- The screen on the north wall shows the menu, crisp at its native size: the pause panel with the menu closed, and,
  with it open, the settings panel, the settings and the focus ring as the live menu has them.
- The light terminal takes no ray while the pointer is over the demo panel: hovering its buttons through the panel
  lights nothing, and a click on the panel is no shot.
- The menu opens over the HUD, and the demo panel and help stay over both.
- The light terminal's buttons light the room warm, cool or in the slider's hue under the cursor; the slider drags
  under the cursor from several camera angles, its fill following the thumb; its rounded corners stay clean as the
  camera moves round it.
- The name plate faces the camera as it moves, and its text stays crisp as the camera comes close.
- The hand shows over the menu's buttons and the terminal's, the I-beam over the name entry in Settings, and the arrow
  comes back over the room.
- In the pause menu the panel's title has Fenrir's icon at its right end, and in Settings the controls list scrolls by
  a thin blue bar.
- The terminal's swatch border and its reading change colour together when Warm, Cool or the hue slider changes the
  lamps, and the name plate has a glowing blue frame round its rounded corners.
- With scripting built, the speedometer sits bottom right over the ammo counter: its needle and the blue share of its
  track rise as the camera flies (WASD, Shift for faster) and fall back when it stops, under a readout of the speed;
  clicking Resume, Settings or Back in the menu logs "the menu's ... was clicked" from C#.
- With scripting built, the pause menu shows the Razor settings page beside the pause panel: its slider, checkbox and
  name entry log their values ("UIDemo (Razor): ..."), and keep them when the menu is closed and opened again; Add a
  binding slides a row in, Remove slides it out, and the credits show under their heading. Saving
  `Settings.razor.css` with the menu open restyles the page; rebuilding the scripts brings the page back after the
  reload. Built without scripting, the pause panel is centred alone, as before.
- With scripting built, the C# sign stands beside the name plate: its button lights under the cursor, a click on it
  logs "UIDemo (C#): the C# sign was clicked" without firing (no hit marker, the ammo unchanged), and it takes no hover
  while the menu is open or the right mouse button turns the camera.
