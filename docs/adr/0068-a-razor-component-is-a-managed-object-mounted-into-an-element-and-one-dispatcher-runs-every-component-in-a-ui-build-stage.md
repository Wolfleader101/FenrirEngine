---
status: accepted
---

# A Razor component is a managed object mounted into an element, and one dispatcher runs every component in a UI build stage

A `.razor` file compiles to a class whose `@code` fields its markup reads (ADR-0067). The engine is ECS-first (ADR-0007): components are data and systems are logic, and per-object behaviours come only "as a layer on top: a behaviour component plus one system that dispatches to it". Native code never calls C# per element (ADR-0039, ADR-0061). This ADR places Razor components inside those rules. Settled in a grilling session on 2026-11-15.

## One managed object per mounted component

- **A Razor component is a `UIComponent` object**, mounted into an element of a UI root: `root.Mount<T>(parent, index, init)` returns a reference to it, and `Unmount()` removes it with its outro. Its fields hold UI-local state only (which tab is open, a typed filter). Game state stays in the ECS and is read when the component builds, through `Context`, the frame's `SystemContext`. Elements stay FenrirUI's (ADR-0066): a component holds `ElementId`s, never elements.
- **Its surface:**
  - `[Parameter]` properties, with `ChildContent` and other `RenderFragment` slots;
  - `@ref` on an element (an `ElementId`, reset to none when the element goes) or on a component;
  - `@key`;
  - the hooks `OnMount`, `OnParametersSet`, `OnBuilt(bool first)` and `OnUnmount`;
  - `Root` and `Element`, its UI root and its own root element.

  Generic components, cascading values, routing, injection and async lifecycle are not in this version.
- **One dispatcher runs them all.** This is the layer ADR-0007 allowed: a C# stage system shipped as one source file, which `Fenrir.UI.Razor.props` (ADR-0067) adds to the script project that imports it. Only a script assembly's own systems are scheduled, and the engine's generators cannot see Razor's output, so the system must be compiled into the script. It drives the `Fenrir.UI.Razor` runtime, which keeps the mounted components.

## Builds run in `Fenrir.UI.Build`, once a frame

FenrirUI routes input in `PreUpdate`, and styles, lays out and paints each root in `PostUpdate`. FenrirUI gains a second hosted stage, `Fenrir.UI.Build` (ADR-0039), the first work of its `PostUpdate`, before transitions and the cascade. C++ joins it through `UIRoots`, as with the paint stage. Each frame the dispatcher:

1. reads the frame's UI events and runs their handlers (ADR-0069);
2. rebuilds every component that needs it, parents before children.

So handlers see this frame's input, and layout sees this frame's builds. A component's `OnPaint(painter, boxes)` override runs in `Fenrir.UI.Paint`, painting into its root element: the dispatcher joins that stage too and calls the components whose type overrides it, found once per type.

## A component rebuilds when something says so

Native code cannot ask C# to rebuild, so every trigger is checked in C#. A component rebuilds:

1. when it is mounted;
2. after `StateHasChanged()`;
3. after one of its own handlers runs;
4. when a parent's rebuild gives it parameters that changed (compared with `Equals`; a `RenderFragment` always counts as changed);
5. when its `BuildHash()` returns a value it did not return last frame. The default is a constant, so it never changes. A HUD returns `HashCode.Combine(health, ammo)` from values it reads from the ECS.

Hover, press and focus never rebuild a component: they are pseudo-classes, styled by the stylesheet. The builder makes a rebuild that changes nothing cost no native calls (ADR-0067), so a component that rebuilds often costs managed time only.

## A script reload drops every component

Component objects, their handlers and the builder's caches belong to the script generation, which a reload releases (ADR-0004, ADR-0009). The `Fenrir.UI.Razor` runtime is shared and never reloads, so it must let go of all of them, or it would root the old generation. When a generation is released:

- every mounted component's elements are removed at once, with no outro;
- the runtime drops every reference to the generation's objects and types, the parameter-setter cache among them;
- UI-local fields are lost.

The new generation's game code mounts again, typically from a system's `OnCreate`, as UIDemo's speedometer already makes its element again once the root has lost it. Data that must survive a reload belongs in ECS components.

## Considered

- **State in ECS components, a `.razor` file as a pure function of queries.** It is ADR-0007 to the letter, but `@code` fields become useless, local UI state needs a component per widget, and it stops being Razor.
- **Fields and component bindings both.** Two ways to hold one piece of state.
- **Building in an ordinary `Update` system.** Elements would lay out the same frame, but in no defined order against C++'s UI work, and every game would schedule its own builds.
- **s&box's rule that a pointer entering, leaving or clicking an element rebuilds it.** `:hover` and `:active` are stylesheet work here, and a rebuild per hover is waste.
- **Re-adopting the old generation's elements after a reload**, rebuilding over them by the same keys, with no flash and no outro. It needs the mounts recorded somewhere that survives, and a file whose sequence numbers moved must not adopt. It is the upgrade path if dropping everything proves too visible.
- **Carrying fields across a reload** (s&box walks the heap to upgrade instances). Reflection-heavy and fragile.

## Consequences

- *Realises ADR-0007's later layer* for UI: one dispatching system and managed objects, beside ECS state, not instead of it.
- *Amends ADR-0039 and ADR-0061:* FenrirUI hosts two stages, `Fenrir.UI.Build` and `Fenrir.UI.Paint`.
- A mounted component's elements are ordinary elements: C++ and other C# can find, style and read them, and anything that changes them by hand is overwritten at the next rebuild that touches that value.
