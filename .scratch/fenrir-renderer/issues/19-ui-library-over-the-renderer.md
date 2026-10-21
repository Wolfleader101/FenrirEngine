# What does the renderer offer a UI library drawn over it?

Type: grilling
Status: resolved
Blocked by: 10, 11

## Question

ImGui lives in a demo editor for now and will later be replaced by the user's own GUI library, so the renderer builds no UI in. What does FenrirRenderer (and FenrirWindow or the input package) offer a UI library that draws over the frame?

- What the library hands the renderer (vertex and index data, textures, scissor rectangles, draw commands) through a public overlay interface: the graphics device is private, and the overlay must let a UI bring its own shaders and vertex layouts (ADR-0016).
- How the library gets input and can claim it (a UI capturing the mouse from game code).
- How the scene can be drawn into a texture the UI shows (an editor's scene view).
- Where the demo's ImGui integration lives (demo scaffold or the demo editor).

## Answer

ADR: `docs/adr/0027-ui-draws-through-fenrirdraw2d-and-takes-input-through-input-routers.md`. Glossary: FenrirDraw2D, 2D draw list, 2D pipeline description, 2D target, Overlay layer, Input router, Input claim, Control owner. All of it goes into the foundation spec; no new tickets.

- **Three responsibilities.**
  - A new plain-data package, **FenrirDraw2D** (no GPU code; depends only on FenrirAssets and FenrirMath), holds the 2D draw data.
  - FenrirRenderer's core has one **2D render feature** that draws that data into a **2D target**.
  - A future FenrirPainter / FenrirUI (an s&box-style `Painter` and retained UI framework) produces the data with no knowledge of the backend.

  The renderer never learns whether data came from C++, C#, ImGui or another library.
- **`DrawList2D`** holds:
  - vertex and index bytes
  - value-type **2D pipeline descriptions**: shader asset, vertex layout, index type, blend mode, input colour space and blend space, with no renderer handles
  - commands, each with a pipeline index, texture slots (`AssetHandle<ImageData>` plus a sampler, so camera targets from ADR-0026 work), a scissor, an index range, a base vertex and parameter bytes

  Coordinates are local and logical, top-left origin, the same units as the mouse; the target converts them. 4 texture slots and 128 parameter bytes are the first version's limits, not the design's.
- **Targets** supply set 0 (`localToClip`, pixel size, content scale), depth, culling and how a scissor is realised. Pipelines are realised lazily and cached by value; the bytes go into per-frame ring buffers. The only target built is the **window overlay**:
  - `Overlay::Submit(WindowId, layer, DrawList2D)` before `Render`, cleared after it
  - layers lowest first, then submission order
  - orthographic, with no depth or culling
  - scissors scaled by the content scale and rounded outwards
  - debug screen text drawn as a fixed top layer
  - `builtin:2d` shader with the ImGui vertex layout
- **Colour:**
  - Blending is linear by default, and the target encodes on write.
  - Input colour space (`Linear`/`sRGB`) is separate from the shader's linear output.
  - Colour and alpha factors are set separately per blend mode (straight `SrcAlpha/1−SrcA` with alpha `One/1−SrcA`; premultiplied `One/1−SrcA` for both). Premultiplying is done in linear space.
  - Premultiplied alpha is preferred for offscreen targets and by the future painter.
  - `blendSpace: Gamma` is an explicit compatibility path, allowed only on 8-bit sRGB targets.
- **Input routers** sit in FenrirInput between the feed and the game-facing state, all in `PreUpdate`. Anything consumed never reaches `Keyboard`/`Mouse`/`Gamepads` or game event readers.
  - Order is by priority, ties by registration. Routers added during dispatch start next frame; routers removed stop at once.
  - A `BeginDispatch` hook lets a UI refresh its hit-testing first.
  - Ownership is per control: repeats and the release follow the press.
  - Motion, scroll and text are dispatched per event, and pointer capture routes a window's pointer events to the capturing router first.
- **Input claims** (`pointer`, `pointerCapture`, `keyboardFocus`, `textInput` per window, plus an app-wide `gamepad`) describe the interaction, readable in C# as `InputClaims`; routers decide consumption.
  - `keyboardFocus` or `pointerCapture` cancels game-held controls with a `cancelled` release.
  - Focus loss cancels everything owned in that window.
  - A removed router's controls end silently.
- **FenrirWindow** gains:
  - cursor shapes, clipboard text, and focus events fed for cancellation
  - SDL text input driven by `textInput` claims (with caret rectangle), else `textInputByDefault` (default `true`)

  IME composition (updates, commit, caret, cancellation) is reserved, not built.
- **ImGui** lives in the demo scaffold as an optional `ImGuiOverlay` router and draw-list producer, using linear blending; never in a package.
- **Tests:**
  - null backend: layers and order, scissor conversion, empty frames, cleanup
  - FenrirInput: router order, add and remove during dispatch, ownership, cancellation on claim, focus loss and router removal, two windows
  - GL: straight and premultiplied blending over non-opaque destinations, layering, scissors at a content scale other than 1
- **Later** (*Not yet specified*):
  - the `Painter` and UI framework
  - image targets (UI painted into a texture that a material samples) and world-space targets
  - sub-rectangle image updates
  - IME composition
  - C#-authored draw lists
