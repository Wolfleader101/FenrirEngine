---
status: accepted
---

# UI data is pushed by systems, UI events are engine events, pointer input is opt-in per element, and UI on the window blends in gamma

Four defaults of FenrirUI that a reader would otherwise assume the other way, each the result of a trade-off, settled in a grilling session on 2026-11-10.

## Data flows by push; there is no binding language in v1

Every retained system in the research offers one of three: observable bindings with modes (Unity, Noesis, Unreal's Viewmodel), a dirty-by-name data model with `{{ }}` in markup (RmlUi, Gameface), or code setting properties. The engine is ECS-first (ADR-0007): a C++ or C# system reads components and sets an element's text, classes or visibility through its handle, found by id or query. That is one mechanism in both languages and no native data model with typed values to keep in step with C#. Markup carries structure and ids only. A binding language belongs to the Razor-style convenience (ADR-0061), where expressions already exist, and is deferred with it.

## UI events are engine events, and C++ may also listen

Every UI event (click, press, release, hover enter and leave, change, focus and blur, submit, scroll) is sent once as an engine event (ADR-0012) carrying its target element, its kind and a value, so C# and C++ systems read it through an `EventReader` the same way. C++ may also attach listeners to an element; listeners run on the target and then bubble to the root with stop-propagation. The queue gets the target's event only: bubbling is for listeners. Native code never calls into C# per element (ADR-0061), which is why the queue is the primary path. *Amended 2026-11-12 (FenrirUI ticket 11):* the kinds are also pointer move, double click, key press and release, text input and document reloaded, as `UIEventKind` lists them. *Amended 2026-11-13 (FenrirUI ticket 14):* and cancel, which a gamepad's B sends at the focused element.

## Pointer input is opt-in per element

Elements take no pointer input unless styled `pointer-events: all` (s&box's default, the opposite of the web's). A game HUD is mostly labels and bars over a world that wants the clicks; the opt-in makes the HUD transparent to input by default and a menu opaque by one rule on its root. A UI root is an input router (ADR-0027): it consumes a pointer input when the element under the pointer takes pointer events, and keyboard and text input when one of its elements has focus, and publishes the matching input claims. *Amended 2026-11-12 (FenrirUI ticket 11):* the pointer inputs it consumes are button presses (their releases with them) and scrolls; motion is no one's (ADR-0027) and always reaches the game, the pointer claim saying it is over the UI. Focus moves by click, by `Tab` order, from code, and by gamepad directional navigation to the nearest focusable element in the stick's direction; a virtual cursor from the stick is a per-root switch. *Amended 2026-11-13 (FenrirUI ticket 14):* a root consumes the gamepad inputs it acts on, and publishes the gamepad claim, while it has the focus or its virtual cursor is on; with no root focused, a root's first direction gives the focus to its first focusable element.

## UI on the window blends in gamma space

Browsers, Figma, Skia by default and s&box blend UI colours and anti-aliasing in sRGB (gamma) space, which is what a designer's gradients and translucent panels were tuned in. The engine's 2D default is linear and ADR-0027 made `Gamma` an opt-in allowed on an 8-bit sRGB target only. A UI root on the window overlay defaults to the `Gamma` blend space; a root drawn into an image or the world defaults to `Linear`, since lit materials read those. Both are a per-root setting.

## Considered

- **A `{{ }}` binding language in markup now.** It is what RmlUi and Gameface offer and what makes markup feel complete, but it needs a native typed data model, name resolution and dirty tracking that C# fills by name, a second way to move data beside components, and the Razor-style effort will want expressions of its own.
- **Web default `pointer-events: auto`.** Every HUD would start by turning it off.
- **Linear blending for all UI.** Physically right and consistent with the 3D frame, and wrong against every reference a UI designer works from.
