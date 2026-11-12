# How does C# define data asset types and reach scene and asset data?

Type: grilling
Status: open
Blocked by: 10, 15

## Question

What does the C# side of data assets and scene files look like, through the existing scripting pipeline (ADR-0042, ADR-0043, ADR-0046)?

- **Defining types:** how a C# class or struct becomes a data asset type (a base class, an attribute, an interface), how its description reaches the registry at admission beside C# components, and how a file names it neutrally beside C++ types.
- **Richer fields in C#:** strings, lists and references in a C# data asset, given that C# components cross as blittable structs today; whether a C# data asset is mirrored into native memory, read through natives, or kept managed with the engine holding only its description.
- **Reaching data:** `Assets.Load<T>` and a typed instance in C#; a C# component holding an `AssetHandle<T>` to a data asset; what happens on script reload when a type's shape changes while files and instances exist.
- **Scenes from C#:** loading a scene file, spawning a prefab, saving, through the facade pattern (ADR-0029, ADR-0043).
- **Generated code:** what the binding generator emits per data asset type, and what is hand-written.

Zoom into [What is a data asset?](10-what-a-data-asset-is.md), [Who loads and saves files?](15-load-save-api-and-ownership.md) and the C# findings of [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md).
