---
status: accepted
---

# The demos' UI is a demo UI kit in the demo scaffold, over FenrirUI, and Dear ImGui leaves the repository

ADR-0027 let Dear ImGui serve the demos "for now", as the first producer of `DrawList2D`, until the engine had a UI of its own. FenrirUI is built (ADR-0060 to ADR-0070). Three demo apps still draw ImGui panels (LightingDemo, RendererDemo's Overlay scene, UIDemo's switches window), and the others print help and live readouts as debug screen text in the renderer's `DebugFont`. This ADR moves every demo onto FenrirUI through one kit and takes ImGui out. It was settled in a grilling session on 2026-11-16 (`.scratch/demo-ui/spec.md`).

## The kit is demo code in DemoScaffold, shaped to be lifted out

The **demo UI kit** lives in `examples/DemoScaffold/` and is installed by the scaffold, which also installs FenrirUI for every demo app. ADR-0063 keeps screen managers and themes out of the engine. A panel of key lists, scene switchers and tutorials is exactly that kind of opinion, so it is not an engine package. Its API names no scaffold type (`DemoScene`, `DemoConfig`): the scaffold feeds it. If a game later wants the same dev panels, lifting the kit into a package is then a move rather than a rewrite. That move is a later decision, taken only once a game asks for it.

## What it offers

- **The demo panel:** one collapsible side panel on the right of the main window, holding the scene switcher, the readouts and the switches, and scrolling when tall. Readouts are name and value rows, pushed by the scene or pulled from a function once a frame, with an optional frame-stats section (frame time, fps and the renderer's draw counts). Switches are rows bound to values: toggle, range (linear or logarithmic), choice, colour, button, text entry and read-only text.
- **The help:** the scaffold's keys, then the app's, then the scene's, at the bottom left. They are given as structured key rows (`{keys, what}`) instead of one line of text. They stay separate from the keys a smoke run presses.
- **The demo tutorial:** a scene's ordered steps, shown in a card at the top centre the first time the scene is entered in a run, never in a smoke run, and reopened from the panel. A step can end itself on a condition the scene reports.
- **Hints, not tooltips:** a row's one-line description shows in a fixed hint line at the foot of the panel while the row is hovered or focused.
- **Scene ownership:** a scene's rows, slot and tutorial go when the scene is left, as its FenrirScene does. The app's rows stay.
- **F1** hides or shows all of the kit.
- **Input:** only the panels take pointer input, so the free-fly camera and the scene click work around them. The kit's root takes no gamepad navigation, so a pad always drives the scene. Its root sits over a demo's own roots on the overlay.
- **One theme:** `demo.css`, hot-reloaded from the source tree, which a demo may add a sheet over. Text uses FenrirText's embedded default face.
- **Left out:** a log or messages area (on-screen messages stay where they are) and floating tooltips.

## The controls FenrirUI lacks are kit composites

LightingDemo needs dropdowns and colour pickers, and FenrirUI has neither. The kit builds them from the controls FenrirUI has: a choice is a row of buttons, or a previous and next stepper when it has more than four options; a colour is a swatch over three sliders. A `select` or colour control in FenrirUI itself is engine work for its follow-ups (`.scratch/fenrir-ui-followups/`), not this kit.

## Debug screen text stays for diagnostics

The renderer's `DebugFont` screen text and on-screen messages stay as ADR-0025 and ADR-0062 left them, drawn above every overlay layer: the renderer keeps no dependency on FenrirText, and moving them onto it is a later cleanup. The demos stop using `ScreenText` as their UI. Two exceptions follow from what is being shown:

- RendererDemo's Debug draw scenes keep it, because they test debug screen text and messages.
- RendererDemo's Overlay and Painter scenes keep their labels, but as painter or FenrirUI text, because those scenes test 2D drawing.

## Dear ImGui is removed last

The kit comes first. Then each demo moves onto it in its own ticket. Then one ticket deletes the scaffold's `ImGuiOverlay`, `DemoConfig::imguiOverlay`, the `examples/DemoScaffold/libs/imgui` submodule and its tests, and rewrites ADR-0027's section on ImGui. `builtin:2d` and its vertex layout stay: the renderer's offscreen-group composite, RendererDemo's Overlay scene and the 2D tests use them. Only the comments that call it "the ImGui layout" change.

## Considered

- **A package of its own that games could use** (a `FenrirDevUI`). Rejected for now. It would be engine code with C# bindings to keep, and it would carry a theme and a screen layout into the engine against ADR-0063, for demos that are its only user.
- **Keeping ImGui for the tweak-heavy demos.** Rejected. Two UIs in the demos means two input routers, two looks, and a submodule kept only for panels the engine's own UI can draw.
- **Moving `DebugFont` onto FenrirText in the same effort.** Rejected. It changes the renderer for no demo-visible gain, and FenrirUI's spec already left it out.
- **Floating tooltips.** Rejected for v1. FenrirUI has no popup layer or anchored positioning, and a fixed hint line serves a demo as well.
