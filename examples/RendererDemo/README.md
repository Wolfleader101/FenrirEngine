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
| 3 | Sky | A plinth with a spinning box and a ring of lit spheres under the scene's `Sky`: the skybox cubemap from the shared assets (`examples/assets/skybox/`, a procedural sky with hills and a sun, loaded from `skybox.cubemap`), or one dusk colour. The free-fly camera can override the `Sky` with a teal colour of its own (`ColourBackground`). | B: the scene's `Sky`, the cubemap or the dusk colour. O: the camera's own background, the teal colour or the scene's `Sky`. |
| 4 | Debug draw | Every FenrirDebugDraw shape, wire and solid, twice: a depth-tested block on the left and an on-top block on the right, both behind a low wall, each shape labelled in world text. A sphere orbits above them with an axes frame, a box, a circle (kept forever) and a label drawn relative to it. Screen text with Latin-1 accents and a `?` for a character outside it, and two keyed on-screen messages updated every frame. | 1, 2, 3: show or hide the Wire, Solid and Labels groups. T: a sphere, a label and an unkeyed message at the orbiter for three seconds. F: an arrow and label kept forever (`DebugDraw::Forever`), one more each press. X: clear the forever group. |

Every scene draws with 4x MSAA (`DemoConfig::msaa`).

## Hand checks

- RendererDemo opens a window on Meshes and materials (`Scene 1/4: Meshes and materials` is logged):
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
- PageDown and PageUp step through the four scenes, wrapping at both ends (`Scene 2/4: Cameras and render targets`,
  `Scene 3/4: Sky`, `Scene 4/4: Debug draw` and `Scene 1/4: Meshes and materials` are logged).
- F1 shows the help panel (app, scene, keys) in white text in the window's bottom-left corner, long lines wrapped, and
  logs it; F1 again hides it. It shows in every scene, over the scene and clear of the on-screen messages.
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
  - O turns the whole background teal, the camera overriding the `Sky` with its own colour, with the shapes still drawn
    over it, whichever sky B has set; O again brings the `Sky` back.
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
    scale set to 150% (the screen text, messages and help panel grow with it).
