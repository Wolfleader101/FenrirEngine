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
| 1 | Meshes and materials | The imported textured box, Khronos's BoxTextured (`SpawnModel`); a cube and a sphere in `builtin:unlit`, then in `builtin:lit`; a cube in RendererDemo's own stripes shader (`shaders/stripes.*`); a cube with no material (the magenta fallback); an empty mesh and a stale one (nothing drawn); three cubes sharing one material; a metallic/roughness grid of spheres behind; the four blend modes side by side over a checkerboard (Opaque, Masked, AlphaBlend, Additive, each a disc fading to its edge); and a double-sided quad turning beside a single-sided one. | L: turn the light 45 degrees. M: the ambient light, dim, bright, then off. V: hide or show the textured box (`visible`). C: the colour of the three cubes' shared material. |
| 2 | Cameras and render targets | A ring of coloured boxes round a spinning white one, seen by its own cameras: a main view, a second view for split screen, an orthographic top-down minimap in the top-right corner over them, and a monitor whose screen shows what a third camera, circling the scene, draws into an image target. The monitor's own camera sees the screen black: a view does not sample its own target (logged once). | L: the layout, cycling one view, side by side and one over the other. O: the minimap's `order`, over or under the main views. P: the main views' projection, perspective or orthographic. |
| 3 | Sky | A plinth with a spinning box and a ring of lit spheres under the scene's `Sky`: the skybox cubemap from the shared assets (`examples/assets/skybox/`, a procedural sky with hills and a sun, loaded from `skybox.cubemap`) or Darcy Wilson's, the legacy DemoApp's sky (`examples/assets/skybox-darcy/`), or one dusk colour. The free-fly camera can override the `Sky` with a teal colour of its own (`ColourBackground`). | B: the scene's `Sky`, the cubemap or the dusk colour. C: the cubemap, the generated skybox or Darcy Wilson's. O: the camera's own background, the teal colour or the scene's `Sky`. T: the camera's tonemapper, AgX, PBR Neutral, ACES or None. E: the camera's exposure, 0, +1, +2 or -1 EV. |
| 4 | Debug draw | Every FenrirDebugDraw shape, wire and solid, twice: a depth-tested block on the left and an on-top block on the right, both behind a low wall, each shape labelled in world text. A sphere orbits above them with an axes frame, a box, a circle (kept forever) and a label drawn relative to it. Screen text with Latin-1 accents and a `?` for a character outside it, and two keyed on-screen messages updated every frame. | 1, 2, 3: show or hide the Wire, Solid and Labels groups. T: a sphere, a label and an unkeyed message at the orbiter for three seconds. F: an arrow and label kept forever (`DebugDraw::Forever`), one more each press. X: clear the forever group. |
| 5 | Overlay | A raw `DrawList2D` on the window overlay (`builtin:2d`): layers (a red, a green and a blue square, submitted in that order on layers 2, 1 and 0), scissors (a quad red at its top and blue at its bottom, faint in full and solid in its top-left quarter, and a yellow strip cut to its middle third) and the four blend modes (one half-transparent orange in each, over black and white stripes). Beside it a spinning cube on a floor, and the scaffold's ImGui overlay (`DemoConfig::imguiOverlay`) with an "Overlay" window: a Label text field (its text is world text over the cube), a Spin speed slider, and what the scene and the input claims say. | Space: pause or resume the cube's spin. C: the cube's colour. Left click: counted as a scene click and logged. None of the three reach the scene while ImGui has the pointer or the keyboard. |
| 6a | Meshes and materials (C#) | Scene 1 again, built and run entirely by C# systems (`scripts/MeshesAndMaterials.cs`) on the scripting extensions: the same entities, materials and keys, with the textured box from C#'s `Renderer.SpawnModel`. The stripes shader, which C# cannot load by path, is embedded in the app and named `builtin:renderer-demo-stripes`. Only with scripting built (`FENRIR_ENABLE_SCRIPTING`), after the C++ scenes. | As scene 1. |
| 6b | Debug draw (C#) | Scene 4 again, spawned and drawn entirely by a C# system (`scripts/DebugDrawScene.cs`) through the `Fenrir.DebugDraw` facade: the same shapes, text, groups, durations and `relativeTo` orbiter, with the same keys. Only with scripting built, after Meshes and materials (C#). | As scene 4. |

Every scene draws with 4x MSAA (`DemoConfig::msaa`) and the camera's default tonemapper, AgX (ADR-0049), checked by hand
in the five C++ scenes on 2026-10-31: they read as before, but softer, and the overlay, drawn after `Output`, is
unchanged. Colours of 1 are no longer white (AgX maps 1 to 0.59), so the unlit cubes and the checkerboard are lighter
and less saturated than their sRGB values, and the LDR skyboxes in Sky are paler and greyer than they were, their
brightest texels well short of white; E in Sky raises the exposure to bring them back. With scripting built the scene
list has seven scenes, so the logged scene numbers below read `/7` instead of `/5`. The ImGui overlay is on in every
scene; only Overlay makes an ImGui window.

## Hand checks

- The window shows Fenrir's icon (`examples/assets/icons/fenrir.png`, `DemoConfig::iconPath`) in its title bar and on
  its taskbar button, not the system's default.
- RendererDemo opens a window on Meshes and materials (`Scene 1/5: Meshes and materials` is logged):
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
- PageDown and PageUp step through the five scenes, wrapping at both ends (`Scene 2/5: Cameras and render targets`,
  `Scene 3/5: Sky`, `Scene 4/5: Debug draw`, `Scene 5/5: Overlay` and `Scene 1/5: Meshes and materials` are logged).
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
    scale set to 150% (the screen text, messages and help panel grow with it).
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
  - Leaving it and coming back spawns it once again: one orbiter and one circle round it, and no forever arrows left
    from before.
- Overlay (`FENRIR_DEMO_SCENE=Overlay`):
  - The raw draw list, drawn on the window the right way up: in Layers the blue square is at the bottom, the green over
    it and the red on top, though red was sent first. In Scissors the solid part is the quad's top-left quarter, red at
    its top fading towards purple (not the blue bottom: the scissor is not flipped), over the faint whole quad, and the
    yellow strip shows only its middle third. In Blend modes: AlphaBlend and Premultiplied look alike, the stripes
    showing through the orange; Additive brightens the black stripes to orange and leaves the white ones white; Opaque
    is solid orange, its alpha ignored.
  - The "Overlay" ImGui window draws over the scene, in ImGui's colours (dark grey-blue window, blue title bar and
    fields) with crisp text, also with Windows' display scale at 150% (the window and its text grow, and stay sharp);
    the help panel (F1) and debug text draw over it.
  - Clicking in the ImGui window never counts a scene click; clicking the scene does (`Overlay: scene click n` is
    logged, and the window's count goes up). Dragging in the ImGui window (the slider, or with the right button held
    anywhere in it) never turns or flies the free-fly camera.
  - Clicking the Label field and typing changes the label over the cube, and Space and C typed there neither pause the
    spin nor change the colour; the window's Claims line says keyboard yes, text yes. Clicking the scene gives the
    keys back: Space and C work again.
  - In the field, Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste through the system clipboard (paste text copied from
    another program). The cursor turns to an I-beam over the field and to a resize arrow over the window's
    bottom-right corner and its edges.
  - With an IME (Windows' Japanese IME, say), typing in the field opens its candidate window at the field's caret.
