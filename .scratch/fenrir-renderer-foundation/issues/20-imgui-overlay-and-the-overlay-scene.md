# 20: The ImGui overlay and RendererDemo's Overlay scene

**What to build:** The demo scaffold's optional `ImGuiOverlay` draws ImGui through FenrirDraw2D and takes input first as an input router, proving the overlay and routers together; RendererDemo's "Overlay" scene shows a raw `DrawList2D` and the ImGui overlay claiming input over the scene (spec: "Demo apps" (scaffold `ImGuiOverlay`, RendererDemo scene 5), "Build" (imgui row), "FenrirInput" (routers, claims); ADR-0027).

- **imgui** as a pinned submodule at `examples/DemoScaffold/libs/imgui`, linked only by the scaffold's `ImGuiOverlay`, with the spec's local overrides.
- **`ImGuiOverlay`:** an input router (keys, text, mouse, scroll; consumes what ImGui wants; publishes `pointer`, `keyboardFocus` and `textInput` with the caret rectangle) and a draw-list producer using `builtin:2d`, sRGB input and straight `AlphaBlend`, with its font atlas as an `ImageData`. Clipboard and cursor shape through FenrirWindow. Optional in `DemoConfig`.
- **RendererDemo scene 5, "Overlay":** a raw `DrawList2D` showing layers, scissors and the blend modes; an ImGui window with a text field and a slider over a scene whose own keys and clicks visibly stop while ImGui has focus or the pointer. Hand checks in the README.

**Blocked by:** 04 (Input routers, control owners and input claims); 19 (FenrirDraw2D and the window overlay).

**Touches:** `examples/DemoScaffold/`, `examples/RendererDemo/`, `.gitmodules`

**Status:** ready-for-agent

- [ ] A scaffold test (null backend, input feed): with the ImGui window under the pointer a click is consumed and does not reach the scene's click event; typing into a focused field claims `textInput` and the scene's keys see nothing; focusing ImGui while a scene key is held cancels it.
- [ ] `FENRIR_DEMO_SMOKE` runs the scene with its keys on null and GL with no errors.
- [ ] Scene 5 by hand: ImGui renders with correct colours and crisp text at the content scale; the raw list's layers, scissors and blend modes look right; typing, clipboard and cursor shapes work; the scene ignores input ImGui took (checked by hand).
