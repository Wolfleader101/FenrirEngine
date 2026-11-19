---
status: accepted
---

# Demo panels are markup and a C++ API, and Razor panels are shared Razor components in slots, wherever keys cover them

Every demo app must still build and run with scripting off (`FENRIR_ENABLE_SCRIPTING`), and a build without scripting has no Razor (ADR-0067). The user wants Razor shown in the demos wherever it is the easier way to write a panel, C++ scenes included. This ADR decides how the demo UI kit (ADR-0071) is authored, and how Razor fits it without making any demo need scripting. It was settled in a grilling session on 2026-11-16.

## The kit itself is native

The kit's files are a markup document and a stylesheet under `examples/DemoScaffold/assets/ui/`, plus a C++ API over them. They cover the panel, the help, the readouts, the switches, the scene switcher and the tutorial card. So every one of them works in every build. A C++ scene fills its rows through that API, and the kit pushes their values into FenrirUI's controls (ADR-0065's push).

## C# reaches the kit through FenrirUI, not new natives

`Fenrir.Demo` gains no native functions:

- The kit's root is on a known overlay layer, which a script finds with `UIRoot.Find`.
- A scene that asks for one gets a **demo slot**: an element with a known id in the demo panel, into which C# mounts a Razor component.
- The kit ships a few shared Razor components, such as a section, a readout, a toggle, a range, a choice and a colour row. They are written over the same classes the C++ rows use, so the two look alike.
- Their `.razor` sources join every demo app's scripts project through a props file that `fenrir_add_demo_app` imports. A Razor component must be compiled into the script that mounts it (ADR-0068), so they are compiled there rather than in a shared assembly.
- A C# scene's tutorial is given on its C++ `DemoScene` entry, like a C++ scene's.

## Razor is allowed anywhere its panel's switches have keys

A Razor panel is allowed in any scene, C++ scenes included, provided every switch on it is also reachable by a key the help lists. Without scripting, the slot shows one line saying the panel needs scripting, and the scene still works through its keys. Every demo app with C# twin scenes gets at least one Razor panel in them, so Razor is shown where C# already lives.

## Considered

- **Natives on `Fenrir.Demo` for adding rows from C#.** This would mean taking the scaffold onto the generated API pipeline (ADR-0042) for a demo-only surface. FenrirUI's own C# contract already does everything a panel needs.
- **A native fallback for every Razor panel.** This doubles the UI work of every such scene. The keys already are the fallback, and the help shows them.
- **Razor only in C# scenes.** Rejected by the user. A C++ scene's panel may be easier to write in Razor, and the rule above keeps it optional.
