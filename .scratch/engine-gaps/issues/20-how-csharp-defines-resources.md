# How does C# define its own resources?

Type: grilling
Status: resolved
Blocked by: none
Settles: [C# cannot define resources](13-csharp-defined-resources.md)

## Question

A scene resource or app resource is one-per-type package state (`GLOSSARY.md`). C++ defines them; C# reads native ones through `Resources` (ADR-0029), and "resources defined in C# by script assemblies" is left for later there and on the renderer map's "More scripting". What does a script assembly get?

- How a C# type declares itself a scene resource or an app resource (an attribute beside `[NativeResource]`, a base type), and how the script report and admission learn of it.
- Where its memory lives (managed, or native pages like C# components) and whether it must be blittable.
- Its lifetime: created on admission, on first use, or explicitly; what happens on script reload when its shape changes; whether a scene resource dies with its scene (a restart resets the run's state for free).
- `Res<T>` system parameters over `Resources.Get`, which the renderer map names beside it.
- How it meets the `fenrir-serialisation` map: ADR-0078 writes described scene-resource types into scene files, and that map's ticket 16 decides how C# types are described. Say what this design leaves to it.

Outcome: a spec-ready design, or a pointer to the `fenrir-serialisation` map's ticket 16 with this question added there.

## Facts (checked 2026-11-19)

- C# reaches resources through `Resources.Get/TryGet/GetReadOnly/Insert/Remove` (`packages/FenrirScripting/Managed/Fenrir.Scripting/Resources.cs`), for `MakeSceneResource` types a native extension registered (`packages/FenrirScripting/README.md:605,687-735,718`). C# can define components (README:238); nothing mentions C#-defined resources.

## Answer

Grilled 2026-11-19 in round 2 of [Engine gaps for Flightless Frenzy](../map.md); every recommendation was accepted. **Spec-ready, designed here.**

- **Declaring one:** a blittable C# struct marked `[SceneResource]` or `[AppResource]`, admitted through the script report beside C# components and kept in native memory like them.
- **Lifetime:** an app resource is made when its assembly is admitted. A scene resource is made with each scene and dies with it, so a level restart resets the run's state for free.
- **On reload,** a resource whose shape changed resets to its defaults, with a warning.
- **Reading and writing** go through the existing `Resources.Get<T>` and its family.
- **Deferred:** `Res<T>` system parameters.
- **Left to the `fenrir-serialisation` map** (its ticket 16): how a C# scene resource is described and written into scene files (ADR-0078).
