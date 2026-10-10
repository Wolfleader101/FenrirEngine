# Spec: .NET hosting library (own Coral/Mono-style API)

**Status:** ready-for-agent

## Goal

Build a general-purpose C++/.NET (CoreCLR) hosting library, similar in purpose to Mono's embedding API and Coral, but designed for our own API usage. Coral and Mono are architectural references only: nothing is copied, and Microsoft's hosting documentation is authoritative for behaviour.

The core library must be usable from **any** C++ project, not just FenrirEngine. Engine (gameplay/ECS) integration begins only after the hosting API is complete and stable.

## Starting point

`packages/FenrirScripting/IMPLEMENTATION_PLAN.md` describes milestones 1–4 as done: managed diagnostics, collectible component loading with dependency resolution, a single `int(int,int)` invocation path, and unload observation. That work was produced by an LLM that was not strong at code or decomposition, so the first tickets review and clean it up before building further.

## Phases

- **A: review and prefactor (01–05).** Audit, record the scope decision, replace per-export name lookup with a bridge function table, move demo calls out of the production bridge, clean up, and establish an engine-independent library boundary.
- **B: the API (06–16).** Type handles, method handles with typed invocation, a host-wide exception/log sink, managed objects with explicit ownership, objects as arguments, fields/properties, strings, blittable structs, arrays, internal calls (managed → native), and type enumeration/relationships/attributes.
- **C: lifetime and stabilisation (17–19).** Reload with root-retention checks, an API stabilisation pass, then a separate engine integration spec.

## Contracts carried forward

- Engine builds with scripting OFF never discover or invoke `dotnet`.
- Public results own their storage. Handles are opaque, validated, never reused, and are not CLR addresses or raw `GCHandle` values.
- Explicit release is a different concept from observed collection. Library operations never force GC.
- CoreCLR has process lifetime. The bridge stays resident; components are collectible.
- Managed exceptions never cross the unmanaged boundary uncaught.
- Calls on an owner must be externally serialized (until a later decision changes this).

## Out of scope (for now)

- **CI / automated cross-platform runners.** We should add them eventually; for now, verification is local and the docs report which platforms were actually tested.
- Thread safety and concurrent calls, async execution.
- Generic types and methods.
- Native-created delegates and managed events.
- Debugger attach, and hot-reload file watching or shadow copying.
- Gameplay/ECS integration (spec only, in ticket 19).

## Tickets

See `issues/`. Frontier at the start: 01, then 02 and 04 in parallel.
