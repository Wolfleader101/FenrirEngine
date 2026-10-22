---
status: accepted
---

# A UI draws through FenrirDraw2D and takes input through input routers

The renderer builds no UI in. ImGui serves the demos for now, and the engine will later have its own retained-mode UI framework (CSS-like layout and styling over a 2D `Painter`, as s&box has) that draws screen-space UI, UI in textures and world-space UI from the same painting. ADR-0016 already says a UI library reaches the renderer only through a public interface, never the graphics device, and that the UI can bring its own shaders and vertex layouts. This ADR decides that interface and how a UI takes input away from game code. The foundation builds only the screen overlay. The aim is that nothing in it blocks the rest.

## Three responsibilities

1. **FenrirDraw2D: 2D draw data.** A new package of plain data with no GPU code. It depends only on FenrirAssets (for asset handles) and FenrirMath, and it knows no renderer, UI or language.
2. **FenrirRenderer: draws 2D draw data into a target.** Its renderer core has one **2D render feature**. The screen overlay is the first target to use it.
3. **A future FenrirPainter / FenrirUI: produces 2D draw data.** It provides shapes, paths, text, images, borders and clipping, and above that elements, layout, styling, focus and state, all with no knowledge of the graphics backend.

The dependencies point one way: UI and painter → FenrirDraw2D ← FenrirRenderer. The renderer never learns whether a draw list came from C++, C#, ImGui or another library.

## FenrirDraw2D

- **`DrawList2D`** holds:
  - vertex bytes and index bytes
  - a table of **2D pipeline descriptions**
  - an ordered list of commands
- **A command** names:
  - a pipeline by index
  - its texture slots, each an `AssetHandle<Image>` plus a sampler description (filter and wrap; linear clamp by default). Camera render targets are image assets (ADR-0026), so a scene view is sampled like any image. *Amended 2026-10-22 (renderer foundation ticket 05):* handles are typed by the short asset name, as C#'s are, so this ADR's `AssetHandle<Image>` and `AssetHandle<Shader>` were `AssetHandle<ImageData>` and `AssetHandle<ShaderData>`.
  - an optional scissor rectangle
  - an index offset and count, and a base vertex
  - a block of per-command parameter bytes
- **A 2D pipeline description** is a plain value with these fields:
  - a shader (an `AssetHandle<Shader>` built by `fenrir_add_shaders`, so GLSL 460 → SPIR-V, ADR-0018)
  - a vertex layout (stride, plus per attribute its location, format and offset)
  - an index type (16 or 32 bit)
  - a blend mode
  - an input colour space
  - a blend space

  A pipeline holds no handle of the renderer's.
- **Local coordinates.** Vertices and scissor rectangles are in **local 2D coordinates**: logical units, top-left origin, Y down, the same units as FenrirInput's mouse. This is the standard for every future painter. A draw list never says where it will be shown.
- **The first implementation's limits:**
  - 4 texture slots per command
  - 128 parameter bytes per command
  - 16- and 32-bit indices

  These are limits of the current version, not of the design. The renderer checks them; a command that exceeds them is skipped and logged once. They can be raised without changing the shape.

## CPU data, GPU realisation, and the target

- **The draw list is CPU data; the renderer realises it.** Each frame the 2D render feature copies the vertex, index and parameter bytes into per-frame ring buffers (ADR-0019). It turns each pipeline description into a device pipeline lazily, cached by value as materials' are (ADR-0024). It resolves texture slots as it does for materials: a missing or stale image binds the default texture. Nothing else outlives the frame.
- **A 2D target supplies the transform and the rules.** Each target provides set 0 of every 2D shader, declared by the engine include `Fenrir/2D.glsl`:
  - a `localToClip` matrix
  - the target's pixel size and content scale

  The target also sets depth testing, culling and how a scissor is realised. A 2D shader is the same for every target. Set 1 is per command: its parameter block at binding 0, laid out from the shader container's reflection, then its textures.
- **Scissors are local rectangles; the target converts them.** On the window overlay a scissor is scaled by the content scale, rounded outwards to whole pixels and clamped to the target, and becomes the hardware scissor. A target that rotates or projects its local space (world space) will realise the scissor otherwise, as a clip in the shader or in stencil. That is why the draw list stores it in local space.

## The window overlay (built)

- **`Overlay`** is an app resource of FenrirRenderer's ECS facade, with `Submit(WindowId, int layer, DrawList2D)`. Submissions can be made at any time before `Render` and are cleared after it.
- **Order:** layers draw lowest first. Within a layer, submissions draw in submission order, and commands in list order.
- **Empty frames:** a window with no submissions draws no UI.
- **Bad windows:** a submission to a window the renderer does not draw (ADR-0022: the main window only, for now) is dropped and logged once.
- **Stage and transform:** the overlay draws in each window's `Overlay` stage, after every view's `Output`. It is orthographic, with `localToClip` mapping the window's logical size to clip space. There is no depth and no culling; those are restrictions of this target, not of FenrirDraw2D.
- **Debug text on top:** debug screen text and on-screen messages (ADR-0025, ADR-0026) are drawn by the same 2D render feature as a fixed layer above every submitted layer, so a UI never hides engine diagnostics.
- **`builtin:2d`:** a textured, vertex-coloured shader with the ImGui vertex layout (position `vec2`, UV `vec2`, colour RGBA8). Simple UIs need no shader of their own.

## Colour and alpha

- **Linear blending is the default.** Images decode on sampling (an sRGB image is an sRGB format), and blending happens in linear space. The target encodes on write: the sRGB backbuffer with encoding on, or a linear target such as RGBA16F written as is.
- **Input and output are separate.** A pipeline's **input colour space** (`Linear` or `sRGB`) describes its vertex colours and colour parameters. The shader's **output** is always linear, in the convention its blend mode names. A custom shader must decode its inputs and produce linear output. `builtin:2d` does this for you: it decodes sRGB vertex colours, treats vertex colours and texels as straight alpha, and premultiplies in linear space when the blend mode is premultiplied.
- **Blend modes, with colour and alpha factors set separately:**

  | Blend mode | RGB (source / destination) | Alpha (source / destination) |
  |---|---|---|
  | `AlphaBlend` (straight) | `SrcAlpha` / `OneMinusSrcAlpha` | `One` / `OneMinusSrcAlpha` |
  | `PremultipliedAlpha` | `One` / `OneMinusSrcAlpha` | `One` / `OneMinusSrcAlpha` |
  | `Additive` | `One` / `One` | `Zero` / `One` |
  | `Opaque` | no blending; the shader's alpha is written | no blending |

  A premultiplied shader multiplies RGB by alpha in linear space, after any sRGB decode.
- **Premultiplied alpha is preferred** for offscreen UI targets, both stored and composited, and the future `Painter` emits it.
- **The gamma compatibility path is opt-in.** A pipeline can set `blendSpace: Gamma`. Encoding is then off and its values pass through and blend in sRGB, as stock ImGui and RmlUi assume. It is allowed only on an 8-bit sRGB target; anywhere else it is a logged error and the pipeline draws with `Linear`. Gamma blending is never implicit.

## Input routing

FenrirInput (ADR-0017) gains **input routers** between the input feed and the game-facing state.

- **The frame's order is fixed:**
  1. FenrirWindow feeds the input.
  2. Routers see each event.
  3. What passes updates `Keyboard`, `Mouse` and `Gamepads` and becomes the game-facing `EventReader` events, tick edges included.

  All of this happens in `PreUpdate`. Nothing a router consumes reaches game state or a game event reader, so captured input cannot leak through a raw reader.
- **Registration:**
  - `AddInputRouter(priority, router)` returns a router id; `RemoveInputRouter(id)` removes it.
  - Routers run highest priority first. Equal priorities run in registration order, so the order is stable.
  - A router added during dispatch sees events from the next frame on.
  - A router removed during dispatch sees no further event, even in the current dispatch.
  - Routers are C++ callbacks. A UI written in C# later registers through the same API via the managed host.
- **Dispatch:**
  - Each event goes to the routers in order, in arrival order, until one returns `Consumed`. If none does, it passes to the game.
  - Before the frame's first event, each router gets a `BeginDispatch` call. A UI can refresh its hit-test data there, so hit-testing against the previous frame's layout (what ImGui does) is allowed but not required.
- **Ownership is per control, not per device.**
  - A press of a key, a mouse button or a gamepad button (and later each touch pointer) belongs to whoever took it: the router that consumed it, or the game if it passed.
  - That control's repeats and its release go straight to the owner, without dispatch. So a release never reaches anyone who did not get the press: no orphan releases and no stale held state.
  - Motion, scroll and text are not owned; each event is dispatched.
  - A UI can own the left button's press without consuming any motion or keyboard event.
- **Pointer capture:** while a router holds `pointerCapture` for a window, that window's pointer events go to it first, and what it passes goes on to the game.
- **Input claims describe the interaction; routers decide consumption.**
  - Each router publishes its claims per window: `pointer` (the pointer is over the UI), `pointerCapture` (a drag that began on the UI), `keyboardFocus` and `textInput` (with a caret rectangle). It also publishes one app-wide `gamepad` claim, for UI navigation.
  - The **`InputClaims`** resource shows the union of the claims, per window. C# can read it.
  - Claims never filter events by themselves.
- **Cancellation:**
  - A router that raises `keyboardFocus` or `pointerCapture` for a window cancels the game-owned controls of that device in that window. The game gets a release marked `cancelled` at once, and the later physical release goes to no one.
  - When a window loses focus, every owned control in it is cancelled: the game and routers alike get `cancelled` releases, and that window's `pointerCapture` is dropped.
  - A router removed while it owns controls has them ended silently (no one else ever had them), and its claims are cleared.
- **Several windows:** events name their window (ADR-0017), so claims and capture are per window. Gamepads are app-wide.

## Window services

FenrirWindow gains, keyed by `WindowId`:

- `SetCursorShape` (arrow, text, the four resize shapes, hand, not-allowed, hidden), beside its cursor modes
- clipboard text get and set
- focus gained and lost, fed to FenrirInput for cancellation

Text input follows the `textInput` claim. After routing, FenrirWindow keeps SDL text input on, with the claimant's caret rectangle, for each window where any router claims it. In other windows it follows `WindowConfig::textInputByDefault`, which is `true` so ADR-0017's text events keep working; a UI app sets it to `false`.

**IME composition is a future requirement.** The design leaves room for:

- composition updates (a preedit string with its cursor and selection), as a reserved FenrirInput event
- committed text, as the existing text event
- caret placement, from the `textInput` claim's rectangle
- cancellation, when focus or the claim goes

## ImGui in the demos

The demo scaffold (ticket 23) has an optional `ImGuiOverlay`. It is the first consumer of `DrawList2D` and the overlay, and it gives demo apps tweak panels; the ImGui editor prototype reuses it.

- It is an input router.
- It builds a `DrawList2D` with `builtin:2d`: input colour space `sRGB`, straight `AlphaBlend`, linear blending, so it tests the default path.
- Its font atlas is an `ImageData` asset, re-uploaded whole when it changes.

ImGui is a submodule linked only by that piece, built only with the examples. It is never part of a package and never the basis of Fenrir's own UI.

## Testing

- **On the null backend, through `RendererProbe`:**
  - submitted layers per window and their order
  - command order and resolved pixel scissors (logical to physical, rounding and clamping)
  - realised pipeline descriptions
  - empty submissions
  - nothing kept between frames but cached pipelines
- **On FenrirInput, with no window:**
  - router priority and stable order
  - consumption
  - adding and removing routers during dispatch
  - press, repeat and release ownership
  - cancellation on claim and on focus loss
  - removing a router that owns controls
  - two windows with independent claims and capture
- **On GL, by reading back the backbuffer and an offscreen target:**
  - straight and premultiplied quads, over opaque and non-opaque destination alpha
  - layer ordering
  - scissor clipping at a content scale other than 1

## Not built, and kept possible

- **The `Painter` and the retained UI framework:** new packages over FenrirDraw2D.
- **An image target:** a draw list painted into an `AssetHandle<Image>` render target, which an ordinary 3D material then samples. This is the first world-space UI. It needs rules for creation, resizing, clearing, and ordering in the graph before the views that sample it. Premultiplied storage is preferred.
- **A world-space target:** a world matrix and the camera's view-projection, depth-tested in a view's `Transparent` stage. The target sets depth and transforms; the draw list doesn't change. Lighting or normal-based effects may need more than `localToClip` in set 0. That is an extension of the set, not of the draw list.
- **Dynamic images:** image assets updated by sub-rectangle (font atlases, runtime UI textures) rather than whole re-upload. The resource API should not assume images are immutable or always uploaded whole.
- **IME composition**, and C# authoring of draw lists through the managed host.

## Considered options

- **The overlay as the whole 2D system.** Rejected: image and world-space UI would need a second path, and a painter would be tied to the screen.
- **`DrawList2D` in FenrirRenderer's public headers, moved out later.** Rejected: a painter or UI would depend on the renderer until then, and the move would break every user.
- **Pipelines as renderer handles inside the draw list.** Rejected: the draw list would depend on a renderer instance, and it would no longer be plain data that any language can build.
- **Gamma-space blending by default** (what ImGui and RmlUi assume). Rejected: it is wrong for HDR and texture targets, and it becomes an implicit global assumption. It stays as an explicit opt-in.
- **Global capture flags that game code checks itself** (ImGui's `WantCaptureMouse`), or a `Raw()` view for the UI. Rejected: every game reader has to remember the check, and missing it leaks clicks through the UI.
- **Ownership per device.** Rejected: a UI holding one mouse button would hide all motion, or all keys, from the game.

## When to revisit

When the `Painter` or UI framework starts (it may want more than four texture slots or 128 parameter bytes), when the first image or world-space UI target is built, when whole-image atlas re-uploads show in a profile, or when IME composition is fed.
