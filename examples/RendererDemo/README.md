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
| 1 | Meshes and materials | The imported textured box, Khronos's BoxTextured (`SpawnModel`); a cube and a sphere in `builtin:unlit`, then in `builtin:lit`; a cube in RendererDemo's own stripes shader (`shaders/stripes.*`); a cube with no material (the magenta fallback); an empty mesh and a stale one (nothing drawn); three cubes sharing one material; a metallic/roughness grid of spheres behind; the four blend modes side by side over a checkerboard (Opaque, Masked, AlphaBlend, Additive, each a disc fading to its edge); and a double-sided quad turning beside a single-sided one. | L: turn the light 45 degrees. M: the sky's grey, which lights the scene, dim, bright, then black. V: hide or show the textured box (`visible`). C: the colour of the three cubes' shared material. |
| 2 | Cameras and render targets | A ring of coloured boxes round a spinning white one, seen by its own cameras: a main view, a second view for split screen, an orthographic top-down minimap in the top-right corner over them, and a monitor whose screen shows what a third camera, circling the scene, draws into an image target. The monitor's own camera sees the screen black: a view does not sample its own target (logged once). | L: the layout, cycling one view, side by side and one over the other. O: the minimap's `order`, over or under the main views. P: the main views' projection, perspective or orthographic. |
| 3 | Sky | A plinth with a spinning box and a ring of lit spheres under the scene's `Sky`: the skybox cubemap from the shared assets (`examples/assets/skybox/`, a procedural sky with hills and a sun, loaded from `skybox.cubemap`) or Darcy Wilson's, the legacy DemoApp's sky (`examples/assets/skybox-darcy/`), or one dusk colour. The sky lights the scene too, except Darcy Wilson's, whose teal sea would tint everything: a `SkyLight` lights it with a neutral grey instead. The free-fly camera can override the `Sky` with a teal colour of its own (`ColourBackground`). | B: the scene's `Sky`, the cubemap or the dusk colour. C: the cubemap, the generated skybox (which lights the scene) or Darcy Wilson's (lit by a grey `SkyLight`). O: the camera's own background, the teal colour or the scene's `Sky`. T: the camera's tonemapper, AgX, PBR Neutral, ACES or None. E: the camera's exposure, 0, +1, +2 or -1 EV. |
| 4 | Debug draw | Every FenrirDebugDraw shape, wire and solid, twice: a depth-tested block on the left and an on-top block on the right, both behind a low wall, each shape labelled in world text. A sphere orbits above them with an axes frame, a box, a circle (kept forever) and a label drawn relative to it. Screen text with Latin-1 accents and a `?` for a character outside it, and two keyed on-screen messages updated every frame. | 1, 2, 3: show or hide the Wire, Solid and Labels groups. T: a sphere, a label and an unkeyed message at the orbiter for three seconds. F: an arrow and label kept forever (`DebugDraw::Forever`), one more each press. X: clear the forever group. |
| 5 | Overlay | A raw `DrawList2D` on the window overlay (`builtin:2d`): layers (a red, a green and a blue square, submitted in that order on layers 2, 1 and 0), scissors (a quad red at its top and blue at its bottom, faint in full and solid in its top-left quarter, and a yellow strip cut to its middle third) and the four blend modes (one half-transparent orange in each, over black and white stripes). Its labels are FenrirText, painted (`SceneLabels`) into a list of their own on overlay layer 3. Beside it a spinning cube on a floor, and the scene's "Overlay input" section in the demo panel: a Label text entry (its text is world text over the cube), a Spin speed slider, readouts of the spin, colour and clicks, and the main window's four input claims. | Space: pause or resume the cube's spin. C: the cube's colour. Left click: counted as a scene click and logged. None of the three reach the scene while the demo panel has the pointer or the keyboard. |
| 6 | Painter | FenrirPainter's analytic shapes, paints and clips (`packages/FenrirPainter`), one painted `DrawList2D` on the window overlay (`builtin:painter`), each shape one quad evaluated in the shader. Row 1: a red rectangle, a blue rounded rectangle with four different radii, a dark rounded box with a border of four widths and colours, and two light cards, one over an outset shadow and one with an inset shadow. Row 2: a green circle, a yellow arc whose sweep breathes, a pink ring, a two-stop gradient, a five-stop rainbow (through the stop texture) and a circle with a radial gradient whose focal point circles inside it. Row 3: Fenrir's icon stretched, nine-sliced into a wide panel, and repeating as a pattern in a circle, and a gradient seen through a rounded clip that turns. Each row's caption and the status line under them are FenrirText in the same painted list (`SceneLabels`). It blends in gamma space, as a UI on the window does (ADR-0065). | G: blend in gamma or linear space (logged). Space: pause or resume the arc, the focal point and the clip. |
| 7a | Meshes and materials (C#) | Scene 1 again, built and run entirely by C# systems (`scripts/MeshesAndMaterials.cs`) on the scripting extensions: the same entities, materials and keys, with the textured box from C#'s `Renderer.SpawnModel`. The stripes shader, which C# cannot load by path, is embedded in the app and named `builtin:renderer-demo-stripes`. Only with scripting built (`FENRIR_ENABLE_SCRIPTING`), after the C++ scenes. | As scene 1. |
| 7b | Debug draw (C#) | Scene 4 again, spawned and drawn entirely by a C# system (`scripts/DebugDrawScene.cs`) through the `Fenrir.DebugDraw` facade: the same shapes, text, groups, durations and `relativeTo` orbiter, with the same keys, and a Razor panel in its demo slot (`scripts/DebugDrawPanel.razor`, mounted by `scripts/DebugDrawPanelSetup.cs`): Wire, Solid and Labels toggles over the three groups. Only with scripting built, after Meshes and materials (C#). | As scene 4; 1, 2 and 3 and the panel's toggles agree. |

Every scene draws with 4x MSAA (`DemoConfig::msaa`) and the camera's default tonemapper, AgX (ADR-0049), checked by hand
in the five C++ scenes before Painter on 2026-10-31: they read as before, but softer, and the overlay, drawn after `Output`, is
unchanged. Colours of 1 are no longer white (AgX maps 1 to 0.59), so the unlit cubes and the checkerboard are lighter
and less saturated than their sRGB values, and the LDR skyboxes in Sky are paler and greyer than they were, their
brightest texels well short of white; E in Sky raises the exposure to bring them back. With scripting built the scene
list has eight scenes, so the logged scene numbers below read `/8` instead of `/6`.

## The demo panel

RendererDemo's switches, readouts and help are the demo scaffold's demo UI kit's (`examples/DemoScaffold/README.md`,
"The demo UI kit"), on FenrirUI.

- **The help**, at the bottom left: the scaffold's keys, then the scene's key rows (`DemoScene::keyRows`), as in the
  table above.
- **The demo panel**, on the right: the scene switcher, then the scene's own:
  - Meshes and materials: a Tutorial button, which shows its tutorial again. The tutorial (the scene list, F1, the
    free-fly camera and the scene's keys) shows the first time the scene is entered in a run, never in a smoke run.
  - Overlay: the "Overlay input" section (the table above).
  - Debug draw (C#): the "Groups (Razor)" panel in the scene's demo slot. Without scripting the slot is not there (the
    scene is not); a build whose script assembly is missing shows the kit's note that the panel needs scripting.
- **Debug screen text** stays where it is what a scene tests: Debug draw and Debug draw (C#) keep their screen text and
  messages. Overlay and Painter label their 2D tests in FenrirText instead.

## Hand checks

- The window shows Fenrir's icon (`examples/assets/icons/fenrir.png`, `DemoConfig::iconPath`) in its title bar and on
  its taskbar button, not the system's default.
- RendererDemo opens a window on Meshes and materials (`Scene 1/6: Meshes and materials` is logged):
  - The tutorial card shows at the top centre, "RendererDemo" with step 1/4 (the scenes). Next steps through F1, the
    free-fly camera and the scene's keys, Back steps back, and Done or Close (or Escape) closes it. Leaving the scene
    and coming back does not show it again; the Tutorial button in the panel's "Meshes and materials" section does, from
    step one.
  - The textured box on the left shows the Cesium logo on each face (its base colour texture) and is lit.
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
    brighter, then black. V hides the textured box and shows it again. C changes the three cubes' colour together.
- Holding the right mouse button and moving the mouse looks around; with it held, WASD flies, Shift flies faster and the
  wheel changes the speed.
- PageDown and PageUp step through the six scenes, wrapping at both ends (`Scene 2/6: Cameras and render targets`,
  `Scene 3/6: Sky`, `Scene 4/6: Debug draw`, `Scene 5/6: Overlay`, `Scene 6/6: Painter` and
  `Scene 1/6: Meshes and materials` are logged).
- The demo UI kit shows in every scene, over the scene and clear of the on-screen messages: the help (app, scene and a
  table of keys) at the bottom left, and the demo panel, under the app's name, on the right. F1 hides both, and F1
  again shows them and logs the help.
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
- Sky (`FENRIR_DEMO_SCENE=Sky`):
  - The skybox surrounds the camera: the sun ahead, up and a little to the right, hills all round the horizon and ground
    below. Looking round with the right mouse button held, the sky turns with the view and never moves with the camera
    as it flies, and there is no seam, crease, gap or brightness step where two faces meet (look at the corners and
    straight up and down), and nothing is mirrored or upside down (the hills stand on the ground, the sun stays above
    them).
  - The plinth and the shapes are drawn over the sky, which fills only what they leave.
  - B turns the sky into one flat dusk purple everywhere, and back into the skybox.
  - C swaps the generated skybox for Darcy Wilson's (logged as `C: the skybox cubemap is Darcy Wilson's`), and back.
    Hers surrounds the camera too, with no seam, crease, gap or brightness step where two faces meet (look at the
    corners and straight up and down), and nothing mirrored or upside down: what is up in her faces is up, the top face
    is overhead and the bottom face underfoot. Pressed while B shows the dusk colour, C picks the skybox B brings back.
  - O turns the whole background teal, the camera overriding the `Sky` with its own colour, with the shapes still drawn
    over it, whichever sky B has set; O again brings the `Sky` back.
  - T steps the camera's tonemapper from AgX through PBR Neutral, ACES and None (each logged as `T: the camera's
    tonemapper is …`), and E steps its exposure through 0, +1, +2 and -1 EV (`E: the camera's exposure is +1 EV`), each
    step doubling or halving the colour before the tonemap. At +2 EV the sky and the lit faces go over 1, and the
    tonemappers part as the golden images in `packages/FenrirRenderer/Tests/OpenGL/goldens/` do: None flattens them
    into white and shifts saturated colours' hue as one channel clips first; AgX keeps detail in them and fades their
    colours towards white; PBR Neutral keeps the hues longest; ACES is the most contrasty and saturated.
- Debug draw (`FENRIR_DEMO_SCENE="Debug draw"`):
  - Both blocks hold every shape: line, ray, arrow, a closed polyline star, three dots of growing size, a box, an
    oriented box, sphere, capsule, cylinder, cone, circle, arc, plane, grid, red-green-blue axes and a frustum, then a
    solid box, sphere, capsule and two triangles, see-through. Lines are two pixels thick and the dots square, whatever
    their distance.
  - The wall hides the bottom row of the left (depth-tested) block where it stands in front of it; the right (on-top)
    block's bottom row shows through the wall. The left block's solids are hidden where the wall is in front of them,
    and blend over what is behind them.
  - Every label faces the screen, stays the same size as the camera flies nearer or further, is never hidden by the
    wall or a shape, and is not drawn once the camera has flown past it (turn round with the right mouse button).
  - The orbiting sphere's axes, box, circle and "relativeTo the orbiter" label move and turn with it.
  - The two keyed messages stay in place at the top-left, newest first, their numbers changing every frame; the screen
    text below them counts frames and seconds, shows the accents in "café, naïve, Ærø" and a `?` for the euro sign.
  - 1, 2 and 3 hide the shapes, the solids and the labels in both blocks, and show them again; keyed message 2 says
    which are on. T draws a pink sphere and "3 seconds" at the orbiter, and a pink message, all gone three seconds
    later. Each F adds a green arrow and "forever #n" label that stay; X removes them all.
  - Text stays crisp, not blurred or blocky, at sizes from the small labels to the titles, and with Windows' display
    scale set to 150% (the screen text and messages grow with it, and the demo UI kit's text stays as sharp).
- Meshes and materials (C#), with scripting built (PageUp twice from the first scene, or
  `FENRIR_DEMO_SCENE="Meshes and materials (C#)"`):
  - It looks the same as Meshes and materials, object for object (step between the two with PageDown and PageUp, or
    run each with `FENRIR_DEMO_SCENE`): the textured box, the unlit and lit primitives, the scrolling stripes, the magenta
    cube and the empty gap, the grid, the four blend modes over the checkerboard and the two turning quads.
  - L, M, V and C do what they do there, logging the same lines.
  - Leaving it and coming back spawns it once again, not twice over.
- Debug draw (C#), with scripting built (PageUp from the first scene, or `FENRIR_DEMO_SCENE="Debug draw (C#)"`):
  - It looks the same as Debug draw, shape for shape (step between the two, or run each with `FENRIR_DEMO_SCENE`): both
    blocks with every wire and solid shape and its label, the wall hiding the left block's bottom row and not the
    right's, the orbiter's axes, box, circle and label, the screen text with its accents and `?`, and the two keyed
    messages.
  - 1, 2, 3, T, F and X do what they do there, logging the same lines.
  - The demo panel shows the "Groups (Razor)" panel (`RendererDemo: the Razor panel is in Debug draw (C#)'s demo slot`
    is logged), with Wire, Solid and Labels toggles, all on. Unticking one hides that group in both blocks, as 1, 2 or
    3 does, and keyed message 2 says it is off; pressing 1, 2 or 3 ticks and unticks the matching toggle. Hovering a
    toggle shows its hint at the panel's foot.
  - Leaving it and coming back mounts the panel again, once.
  - Leaving it and coming back spawns it once again: one orbiter and one circle round it, and no forever arrows left
    from before.
- Overlay (`FENRIR_DEMO_SCENE=Overlay`):
  - The raw draw list, drawn on the window the right way up: in Layers the blue square is at the bottom, the green over
    it and the red on top, though red was sent first. In Scissors the solid part is the quad's top-left quarter, red at
    its top fading towards purple (not the blue bottom: the scissor is not flipped), over the faint whole quad, and the
    yellow strip shows only its middle third. In Blend modes: AlphaBlend and Premultiplied look alike, the stripes
    showing through the orange; Additive brightens the black stripes to orange and leaves the white ones white; Opaque
    is solid orange, its alpha ignored.
  - The labels ("Layers: sent 2, 1, 0", "Scissors", the blend title and the four mode names under the orange
    rectangles) read as before, in the same places, now in the kit's monospaced FenrirText face rather than the
    debug font. With Windows' display scale at 150% they stay crisp and beside their tests, which grow with the
    window's logical units (debug screen text was placed in pixels, so it drifted from them).
  - The demo panel's "Overlay input" section shows the explanation, the Label entry, the Spin speed slider and the
    Spin, Colour and Scene clicks readouts, then the four claims under "Input claims", and no other window draws.
  - The claims readouts change as the pointer crosses the panel: Pointer reads yes over the panel and no over the
    scene; pressing on the slider and holding reads Capture yes until the release.
  - Clicking in the panel never counts a scene click; clicking the scene does (`Overlay: scene click n` is logged, and
    the Scene clicks readout goes up). Dragging the slider never turns or flies the free-fly camera, and changes how
    fast the cube spins.
  - Clicking the Label entry and typing changes the label over the cube, and Space and C typed there neither pause the
    spin nor change the colour; the claims read Keyboard yes and Text input yes. Clicking the scene gives the keys
    back: Space and C work again, and the Spin and Colour readouts follow them.
- Painter (`FENRIR_DEMO_SCENE=Painter`):
  - Every edge is smooth, one pixel soft, with no stair steps or seams: the rounded corners, the circle, the arc's ends,
    the ring's two edges, the border's inner and outer edges, the turning clip's corners as it rotates, and also with
    Windows' display scale at 150%.
  - The rounded rectangle's corners are four sizes: sharp at the bottom left, largest at the bottom right. The border's
    four sides are four colours and widths (thin yellow top, thick pink bottom), meeting on the corners' diagonals.
  - The outset shadow is soft, falls down and to the right of its card and is not seen through it; the inset shadow
    darkens the inside of its card along the top and left edges.
  - The arc grows and shrinks from the top, clockwise, with flat ends; the five-stop gradient runs red, yellow, green,
    blue, magenta along the diagonal; the radial gradient's bright point circles inside its disc and never leaves it.
  - The icon is stretched in the first square; in the wide panel its corners keep their shape and only its middle
    stretches, with no seam lines between the nine pieces; in the circle it repeats as small tiles.
  - The orange-to-teal gradient shows only inside the turning rounded rectangle, which stays rounded as it turns.
  - The three row captions over the rows and the status line under them ("Blending in gamma space (G for linear)")
    read as before, in the same places, in the kit's FenrirText face rather than the debug font, and G does not
    change how they look (they blend in gamma space, in a list of their own).
  - G switches to linear blending (logged as `G: the painter blends in linear space`): the gradients' middles and the
    shadows' soft parts look lighter, the solid colours unchanged; G again switches back. Space pauses the arc, the focal
    point and the clip, and resumes them.
