---
status: accepted
---

# Razor handlers and two-way binding ride the UI event queue

UI events reach C# as engine events, read through `EventReader<UIEvent>`, and the queue carries each event at its target only. Bubbling is for C++ listeners (ADR-0065). ADR-0065 also deferred a binding language to the Razor-style effort, "where expressions already exist". This ADR says how Razor's `@onclick` and `@bind` work over that queue. Settled in a grilling session on 2026-11-15.

## Handlers

- **Names and kinds.** Blazor's directive names, one for each `UIEventKind`:
  - pointer: `@onclick`, `@ondblclick`, `@onpointerdown`, `@onpointerup`, `@onpointerenter`, `@onpointerleave`, `@onpointermove` and `@onwheel`;
  - keys and text: `@onkeydown`, `@onkeyup` and `@ontextinput`;
  - focus: `@onfocus` and `@onblur`;
  - controls: `@onchange`, `@onsubmit` and `@oncancel`.

  The stand-in's `EventHandlers` class lists them (ADR-0067). A handler is `Action` or `Action<UIEvent>`, or one returning a `Task` that has finished when it returns, such as `e => ValueChanged.InvokeAsync(e.Value)`. There are no async handlers, because a stage has no synchronisation context: one still running when it returns fails as a throwing handler does (logged, its mount turned off).
- **Recorded at build, run by the dispatcher.** The builder records each handler against its element and kind. In `Fenrir.UI.Build` (ADR-0068) the dispatcher reads the frame's UI events once. For each event it walks from the target up through `GetParent` to the nearest element with a handler of that kind, and runs that handler. Exactly one handler runs, so there is no stop-propagation. The handler's Razor component then rebuilds.
- **Pointer events stay opt-in.** An element with a pointer handler takes the pointer only when styled `pointer-events: all` (ADR-0065). The builder does not set it. In a debug build it logs once for a pointer handler on an element whose resolved style takes no pointer events (the native `UIRoot.WarnIfTakesNoPointer` reads the style and warns, since only the engine knows it is a debug build; the builder queues one check per element for the frame after its first layout), so the stylesheet keeps the one rule that makes a menu opaque.

## Two-way binding

- **`@bind` on value controls.**
  - `@bind="volume"` works on `slider` and `progress` (their value), `checkbox` (checked) and `textentry` (its text). The stand-in's `BindAttributes` declares those tags.
  - When the field differs from what the builder last pushed, the builder pushes it (`SetValue` or `SetEntryText`).
  - On that element's `Change` event, the dispatcher reads the control's value into the field, runs `@bind:after` if one is given, and rebuilds the Razor component. A `Change` whose value is the one the builder last pushed or read (a slider drag's closing `Change` repeats its value) moves nothing: no setter, no `@bind:after`, no rebuild.
  - Each side moves only when it changed, so nothing polls both sides every frame, as s&box's `:bind` does.
- **`@bind-X` on a child Razor component's parameters** is standard Razor: a `[Parameter] X` with an `XChanged` callback.
- **Not in this version:** `@bind:event`, format strings and culture, and any binding in markup documents, which still carry structure and ids only (ADR-0065).

## Considered

- **s&box's `Value:bind=@x`.** The stock compiler cannot parse it, so it needs a fork (ADR-0067).
- **No binding: `value="@x"` and an `@onchange` that writes back.** Every settings slider would be two attributes and a lambda.
- **Bubbling to every ancestor's handler, with stop-propagation.** Razor UIs rarely handle one event at two levels, and one handler per event keeps the rebuild after it to one Razor component.
- **Setting `pointer-events: all` inline when an element has a pointer handler.** Inline style beats the stylesheet, so a stylesheet could no longer make the element transparent to the pointer.

## Consequences

- *Amends ADR-0065:* its deferred binding language is Razor's `@bind`, in C# only. Markup stays without bindings.
- A handler's lambda is a new delegate each build when it captures a loop variable. The builder replaces the recorded handler every build anyway, and that costs nothing native.
