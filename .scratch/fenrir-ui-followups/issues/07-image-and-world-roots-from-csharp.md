# 07: C# makes image and world roots and gives a world root its pointer ray

**What to build:** the rest of story 64's "interchangeable": C# makes every kind of UI root C++ makes (spec: "Image and world roots from C#"). Decided with the user on 2026-11-16: build it, amending ADR-0061.

- **Shared structs** in `ScriptUI.hpp`, mirrored by the binding: `ImageRootSettings` and `WorldRootSettings` with the spec's fields and the C++ defaults, documented.
- **Natives,** facades on the `UIRoot` mirror: `CreateImageRoot`, `CreateWorldRoot`, `GetImage`, `SetPointerRay`, with the spec's failures (an invalid root or a no-op, each warned once per cause). The extension owns the `UIImageTarget` or `UIWorldTarget` it makes and ends it with the root, whether `Destroy` or the script generation's release ends it. `UIRoot.Find` is unchanged.
- **The API description** regenerates the natives, the mirror's methods and the shared structs; nothing is hand-written in the generated C#.
- **ADR-0061** gains a dated amendment (C# makes every kind of root through natives on the `UIRoot` mirror, the extension owning the targets); the FenrirUI README's C# section and the glossary's *UI root* if it names what C# can make.
- **UIDemo's C# variant:** a new script makes a small sign in the room (an entity with `WorldQuad2D` beside the name plate), a world root on it from a markup document of its own with one button, and gives it a ray each frame from the window camera through the cursor (`Renderer.ScreenPointToRay`), under the same rules as the terminal (no ray while the menu is open or the right mouse button turns the camera); its click logs "the C# sign was clicked". The README's C# variant and hand checks say so.

**Blocked by:** 01 (One CaptureTarget and test cleanups).

**Touches:** `packages/FenrirUI/Scripting/`, `packages/FenrirUI/Managed/Fenrir.UI/`, `packages/FenrirUI/README.md`, `docs/adr/0061-the-ui-stack-is-native-in-fenrirtext-fenrirpainter-and-fenrirui-and-csharp-is-a-client-through-generated-bindings.md`, `GLOSSARY.md`, `examples/UIDemo/scripts/`, `examples/UIDemo/assets/ui/`, `examples/UIDemo/README.md`

**Status:** ready-for-agent

- [ ] From C#, an image root of 256 by 128 gives a non-empty image of that size, a label in it records draws, and `Destroy` unloads the image.
- [ ] From C#, a world root on a quad entity hovers and clicks a button by ray, and the click reaches C# as a `UIEvent`.
- [ ] `GetImage` on a window root is empty; `SetPointerRay` on a window root is a no-op warned once; `CreateWorldRoot` on an entity without `WorldQuad2D` gives an invalid root warned once.
- [ ] Releasing the script generation ends the roots and targets it made.
- [ ] The binding's generated files are up to date (the generator's check passes).
- [ ] Hand check: in UIDemo with scripting on, the sign's button lights on hover and logs on click.
