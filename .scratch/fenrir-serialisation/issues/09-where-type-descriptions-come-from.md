# Where do the type descriptions that read and write files come from?

Type: grilling
Status: open
Blocked by: 04, 08

## Question

What describes a type's fields when a scene file or data asset is read or written, and when an editor later shows it? **ADR.**

- Is it the build-time API description (ADR-0042) widened with the kinds data assets need (strings, variable-length lists, maps, nested lists of structs, optional values, typed references to assets and data assets, polymorphic values), a run-time type registry the engine fills from those descriptions (and from the C# script report for script types), both, or a new mechanism (macros, intrusive templates)? What does a game developer write per type and per field in C++, and in C#?
- Does a game project run `describe-api` on its own headers? If so, how it joins the game's build and where its output goes; if not, how C++ game types are described.
- Which types are describable: do plain-data components keep their fixed-layout rules, and data assets get richer rules, or does one rule cover both? What is refused with a diagnostic.
- How is a type named in a file so that C++ and C# types, and a future binding's, share one namespace, and how a renamed type is still read.
- Editor metadata on fields (ranges, display names, categories, read-only), from doc comments and from attributes, in both languages.
- Where the registry lives (FenrirCore, FenrirECS, a new package) and what depends on it.

Consult `codebase-design` for where the registry sits. Zoom into [How do C++ engines describe their types for serialisation and editors?](04-how-engines-reflect-types-for-files.md) and [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md) for the facts.
