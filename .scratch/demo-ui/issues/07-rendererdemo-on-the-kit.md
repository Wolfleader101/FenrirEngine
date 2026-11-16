# 07: RendererDemo on the kit

**What to build:** RendererDemo's Overlay scene's ImGui window becomes a scene section. Its 2D test labels and the Painter scene's captions are drawn by FenrirText, not debug screen text. Every scene gets key rows, the first scene a tutorial, and a C# twin a Razor panel. The Debug draw scenes keep their screen text and messages, which are what they test. RendererDemo then sets no `imguiOverlay`.

**Spec:** `.scratch/demo-ui/spec.md`, "Per-demo moves" (RendererDemo), "Debug screen text stays for diagnostics" (ADR-0071). The inventory is `.scratch/demo-ui/research/inventory.md`.

- **The Overlay scene (`OverlayScene.cpp`):**
  - The ImGui window becomes a scene section: the explanation as `Text`, a `TextEntry` feeding the cube's `WorldText` label, a spin-speed `Range` 0..5, readouts for the spin, colour and clicks, and a readout for each of the four input claims.
  - Its key help's "stop while ImGui has the pointer or keyboard" becomes "while the demo panel has them".
- **Overlay and Painter labels:**
  - The labels (layers, scissors, the blend-mode names and title) and the Painter scene's row captions and status line become painter text (FenrirPainter's text draw) in the scenes' own lists, or a FenrirUI root of the scene's under the kit, whichever keeps each scene's test content clear.
  - Positions stay where they are.
  - No `ScreenText` is left in either scene.
- **The Debug draw scenes** (C++ and C#) keep their `ScreenText` and messages unchanged and gain key rows.
- **Key rows** for every scene. The copied key help of the C# twins goes into shared rows.
- **Razor:** Meshes and materials (C#) or Debug draw (C#) mounts a Razor panel in its slot (`razorPanel`), with switches that are also on its keys.
- **A tutorial** for Meshes and materials, the first scene: the scene list, F1, the free-fly camera, the scene's keys.
- **`main.cpp`** drops `imguiOverlay = true`.
- **README:** a "The demo panel" section and rewritten hand checks for the Overlay, Painter and the Razor panel.

**Blocked by:** 05

**Touches:** `examples/RendererDemo/`

**Status:** ready-for-agent

- [ ] No ImGui call or include remains in `examples/RendererDemo/`. No `ScreenText` remains outside the Debug draw scenes.
- [ ] `RendererDemo-Smoke-null` passes with scripting off and on.
- [ ] Hand checks, on OpenGL:
  - the Overlay scene's label entry changes the cube's label
  - Space and C stop while the entry has the keyboard
  - the claims readouts change as the pointer crosses the panel
  - the Overlay and Painter labels read as before, in FenrirText
  - the Razor panel works and its switches' keys do the same
  - the tutorial shows on the first scene
- [ ] Clang and MSVC x64 debug build without new warnings.
