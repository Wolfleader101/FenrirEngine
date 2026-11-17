# 04: Demo tutorials

**What to build:** a scene can give a step-by-step tutorial that shows in a card the first time the scene is entered, steps by hand or by itself, and can be reopened from the panel.

**Spec:** `.scratch/demo-ui/spec.md`, "Demo tutorials".

- **The types:** `DemoTutorial` and `DemoTutorialStep` (title, text, key rows, an optional `done`), and `DemoScene::tutorial`.
- **The card:**
  - top centre, under the messages' column
  - the step's title, text and key rows, with Back, Next (Done on the last) and Close
  - Escape closes it while it is shown
  - pointer input on its box only
  - it comes in and goes out through `:intro` and `:outro`
- **Showing:**
  - the first entry of a scene in a run shows its tutorial
  - later entries don't
  - never in a smoke run or with `FENRIR_DEMO_KEYS`
  - a "Tutorial" button in the scene's section shows it again from its first step
- **Stepping:** `done`, when given, is asked each frame while its step shows. True moves on, and after the last step closes the card.
- **Demos:** none gets a tutorial here; the demo tickets write them. A scaffold test scene shows it.
- **README:** DemoScaffold's README documents tutorials.

**Blocked by:** 03

**Touches:** `examples/DemoScaffold/`

**Status:** resolved

- [x] Scaffold tests on the null backend:
  - a tutorial shows on the scene's first entry and not on its second
  - it never shows in a smoke run
  - Next, Back and Close step and close it
  - a step whose `done` turns true moves on
  - the Tutorial button shows it again from step one
  - a press on the card sends no scene click, and one beside it does
- [x] Every demo's `Smoke-null` passes with scripting off and on.
- [x] Clang and MSVC x64 debug build without new warnings. (MSVC x64 debug built locally, scripting on and off; Clang was not built here.)
