# 20: The ImGui overlay and RendererDemo's Overlay scene

**What to build:** The demo scaffold's optional `ImGuiOverlay` draws ImGui through FenrirDraw2D and takes input first as an input router, proving the overlay and routers together; RendererDemo's "Overlay" scene shows a raw `DrawList2D` and the ImGui overlay claiming input over the scene (spec: "Demo apps" (scaffold `ImGuiOverlay`, RendererDemo scene 5), "Build" (imgui row), "FenrirInput" (routers, claims); ADR-0027).

- **imgui** as a pinned submodule at `examples/DemoScaffold/libs/imgui`, linked only by the scaffold's `ImGuiOverlay`, with the spec's local overrides.
- **`ImGuiOverlay`:** an input router (keys, text, mouse, scroll; consumes what ImGui wants; publishes `pointer`, `keyboardFocus` and `textInput` with the caret rectangle) and a draw-list producer using `builtin:2d`, sRGB input and straight `AlphaBlend`, with its font atlas as an `ImageData`. Clipboard and cursor shape through FenrirWindow. Optional in `DemoConfig`.
- **RendererDemo scene 5, "Overlay":** a raw `DrawList2D` showing layers, scissors and the blend modes; an ImGui window with a text field and a slider over a scene whose own keys and clicks visibly stop while ImGui has focus or the pointer. Hand checks in the README.

**Blocked by:** 04 (Input routers, control owners and input claims); 19 (FenrirDraw2D and the window overlay).

**Touches:** `examples/DemoScaffold/`, `examples/RendererDemo/`, `.gitmodules`

**Status:** resolved

Hand check: Overlay (`FENRIR_DEMO_SCENE=Overlay`), on the window: the raw draw list's layers (blue under green under red), scissors (the solid top-left quarter is the quad's red top, so the scissor is not flipped; the yellow strip's middle third) and blend modes (AlphaBlend and Premultiplied alike, Additive brightening the black stripes, Opaque solid) look right, as examples/RendererDemo/README.md lists. (Checked once by screenshot at 100% scale.)
Hand check: the "Overlay" ImGui window draws over the scene in ImGui's colours with crisp text, also at 150% display scale; debug text and the F1 help draw over it.
Hand check: clicking, typing and dragging in ImGui never reach the scene: no scene click is counted, Space and C typed into the Label field neither pause the spin nor change the colour, and dragging the slider or right-dragging in the window never moves the free-fly camera; clicking the scene gives the keys back. (Clicking and typing checked once with synthetic input.)
Hand check: in the Label field, Ctrl+C/X/V go through the system clipboard; the cursor is an I-beam over the field and a resize arrow on the window's edges and corner; an IME's candidate window opens at the field's caret.

- [x] A scaffold test (null backend, input feed): with the ImGui window under the pointer a click is consumed and does not reach the scene's click event; typing into a focused field claims `textInput` and the scene's keys see nothing; focusing ImGui while a scene key is held cancels it.
- [x] `FENRIR_DEMO_SMOKE` runs the scene with its keys on null and GL with no errors.
- [ ] Scene 5 by hand: ImGui renders with correct colours and crisp text at the content scale; the raw list's layers, scissors and blend modes look right; typing, clipboard and cursor shapes work; the scene ignores input ImGui took (checked by hand).
